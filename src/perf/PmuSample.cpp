// crucible::perf::PmuSample — libbpf binding implementation.
//
// Second per-program facade in the GAPS-004 series (after the
// keystone SenseHub aggregator and the SchedSwitch off-CPU
// drill-down).  Mirrors the SchedSwitch loader shape closely so
// that GAPS-004x BpfLoader extraction has TWO real instances to
// generalize from (Mike Acton's "generalize from ≥2 cases" rule).
//
// Specifics for PmuSample:
//   • 8 SEC("perf_event") programs (one per PmuEventType).  Attach
//     mechanism is perf_event_open() + bpf_program__attach_perf_event(),
//     NOT plain bpf_program__attach() (which the tracepoint-based
//     loaders use).
//   • Each program tracks the target PID across all CPUs via
//     perf_event_open(attr, pid=getpid(), cpu=-1, ...).  No per-CPU
//     loop — kernel-side perf scheduler handles CPU migration.
//   • IBS event types (IbsOp, IbsFetch) require AMD hardware.
//     The dynamic PMU type ID is read from /sys/bus/event_source/
//     devices/ibs_{op,fetch}/type — non-AMD systems get -1 from
//     access() and we skip those attachments without failing load.
//   • All 8 events flow into one shared mmap'd ring buffer
//     (pmu_sample_buf), giving a unified time-ordered timeline.

#include <crucible/perf/PmuSample.h>

#include <crucible/safety/Mutation.h>  // safety::WriteOnce / WriteOnceNonNull / Monotonic
#include <crucible/safety/Pinned.h>    // safety::NonMovable<T>
#include <crucible/safety/Tagged.h>    // safety::Tagged<T, Source>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <bpf/libbpf_legacy.h>  // libbpf_get_error
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

#include <inplace_vector>

extern "C" {
extern const unsigned char pmu_sample_bpf_bytecode[];
extern const unsigned int  pmu_sample_bpf_bytecode_len;
}

namespace crucible::perf {

namespace {

// ── TU-private provenance source tags ───────────────────────────────
namespace source {
    struct Kernel  {};
    struct BpfMap  {};
    struct PerfEvent {};  // FD returned by perf_event_open()
}

using Tgid    = ::crucible::safety::Tagged<uint32_t, source::Kernel>;
using Tid     = ::crucible::safety::Tagged<uint32_t, source::Kernel>;
using BpfFd   = ::crucible::safety::Tagged<int, source::BpfMap>;
using PerfFd  = ::crucible::safety::Tagged<int, source::PerfEvent>;
static_assert(sizeof(Tgid)   == sizeof(uint32_t));
static_assert(sizeof(Tid)    == sizeof(uint32_t));
static_assert(sizeof(BpfFd)  == sizeof(int));
static_assert(sizeof(PerfFd) == sizeof(int));

[[nodiscard]] Tgid current_tgid() noexcept {
    return Tgid{static_cast<uint32_t>(::getpid())};
}

// ── Env-var knobs (cached once at first touch) ──────────────────────
//
// Same caching trap as SenseHub.cpp / SchedSwitch.cpp documents:
// function-local-static captures the env value at FIRST query and
// never re-evaluates.  setenv() AFTER the first call has no effect.
[[nodiscard]] bool env_true(const char* name) noexcept {
    const char* v = std::getenv(name);
    return v != nullptr && v[0] == '1';
}
[[nodiscard]] bool env_true_either(const char* canonical,
                                   const char* legacy) noexcept {
    return env_true(canonical) || env_true(legacy);
}
[[nodiscard]] bool quiet()   noexcept {
    static const bool kQuiet =
        env_true_either("CRUCIBLE_PERF_QUIET", "CRUCIBLE_BENCH_BPF_QUIET");
    return kQuiet;
}
[[nodiscard]] bool verbose() noexcept {
    static const bool kVerbose =
        env_true_either("CRUCIBLE_PERF_VERBOSE", "CRUCIBLE_BENCH_BPF_VERBOSE");
    return kVerbose;
}

// ── Sample-period env-var overrides (GAPS-004c-AUDIT, #1290) ───────
//
// Same caching trap as quiet/verbose — function-local-static
// captures the env value at FIRST query and never re-evaluates.
// setenv() AFTER load() has no effect on the periods that get
// passed to perf_event_open.  This is intentional: the periods
// are baked into the perf_event_attr at attach time and can't
// be changed without re-attaching; caching at first call is a
// no-op vs always-getenv() for the cold path.
//
// Returns the fallback if the env var is missing, empty, malformed,
// negative, zero, or unparseable.
//
// GAPS-004c-AUDIT-2 (2026-05-04) tightening: the v1 implementation
// claimed "negative-value strings parse to 0 via strtoul truncation"
// — that is INCORRECT.  strtoull("-5", ...) recognizes the leading
// '-' and returns 0 - 5 in unsigned arithmetic = ULLONG_MAX - 4 ≈
// 18 quintillion.  Passed as sample_period this means "sample once
// every 18 quintillion events" = effectively never.  User who set
// PERIOD_HW=-5 would get silent suppression — bad UX.  Explicit
// '-' check rejects this and falls back to the spec table default.
[[nodiscard]] uint64_t env_period(const char* name, uint64_t fallback) noexcept {
    const char* v = std::getenv(name);
    if (v == nullptr || v[0] == '\0' || v[0] == '-') return fallback;
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(v, &end, 10);
    if (end == v || parsed == 0) return fallback;  // unparseable / zero → default
    return static_cast<uint64_t>(parsed);
}
[[nodiscard]] uint64_t period_hw(uint64_t fallback) noexcept {
    static const uint64_t kP = env_period("CRUCIBLE_PERF_PMU_PERIOD_HW", fallback);
    return kP;
}
[[nodiscard]] uint64_t period_ibs(uint64_t fallback) noexcept {
    static const uint64_t kP = env_period("CRUCIBLE_PERF_PMU_PERIOD_IBS", fallback);
    return kP;
}
[[nodiscard]] uint64_t period_sw(uint64_t fallback) noexcept {
    static const uint64_t kP = env_period("CRUCIBLE_PERF_PMU_PERIOD_SW", fallback);
    return kP;
}
// Spec table default carries the original period; this helper
// dispatches to the right env override based on the spec's perf_type
// (or is_dynamic for IBS).  Called once per event spec at load time.
[[nodiscard]] uint64_t resolve_period(uint32_t perf_type, bool is_dynamic,
                                      uint64_t default_period) noexcept {
    if (is_dynamic)                           return period_ibs(default_period);
    if (perf_type == PERF_TYPE_SOFTWARE)      return period_sw(default_period);
    return period_hw(default_period);  // PERF_TYPE_HARDWARE / HW_CACHE
}

int libbpf_log_cb(enum libbpf_print_level, const char* fmt, va_list args) noexcept {
    if (!verbose()) return 0;
    return std::vfprintf(stderr, fmt, args);
}

void install_libbpf_log_cb_once() noexcept {
    static std::once_flag once;
    std::call_once(once, [] { libbpf_set_print(libbpf_log_cb); });
}

[[nodiscard]] struct bpf_map* find_rodata(struct bpf_object* obj) noexcept {
    struct bpf_map* map = nullptr;
    bpf_object__for_each_map(map, obj) {
        const char* n = bpf_map__name(map);
        if (n == nullptr) continue;
        const std::string_view name{n};
        if (name.ends_with(".rodata")) return map;
    }
    return nullptr;
}

// ── perf_event_open helper ──────────────────────────────────────────
//
// glibc doesn't expose perf_event_open as a function — it's a
// syscall wrapper everyone has to write themselves.  Standard
// idiom from `man 2 perf_event_open`.
[[nodiscard]] long perf_event_open_syscall(struct perf_event_attr* attr,
                                           pid_t pid, int cpu, int group_fd,
                                           unsigned long flags) noexcept {
    return ::syscall(SYS_perf_event_open, attr, pid, cpu, group_fd, flags);
}

// ── PMU type lookup for AMD IBS ─────────────────────────────────────
//
// IBS events use a dynamic PMU type ID, allocated by the kernel
// at boot when the ibs_op/ibs_fetch driver registers.  Read it
// from sysfs.  Returns -1 if the file doesn't exist (non-AMD
// system) or can't be parsed.
[[nodiscard]] int read_dynamic_pmu_type(const char* path) noexcept {
    std::ifstream file(path);
    if (!file.is_open()) return -1;
    int type = -1;
    file >> type;
    return file.fail() ? -1 : type;
}

// ── PMU event spec table ────────────────────────────────────────────
//
// One row per BPF program section name, mapping it to the
// perf_event_attr that should be opened to feed that program.
// Sample periods are conservative defaults — the user can edit
// the table if they want different rates.
struct PmuEventSpec {
    const char* prog_name;     // BPF program section (matches SEC name in .bpf.c)
    uint32_t    perf_type;     // PERF_TYPE_HARDWARE / SOFTWARE / HW_CACHE / dynamic
    uint64_t    perf_config;
    uint64_t    sample_period;
    bool        is_dynamic;    // true → perf_type read from sysfs at load time
    const char* dynamic_path;  // sysfs path for is_dynamic events
    const char* friendly_name; // for diagnostics
};

// HW cache events use a packed config: cache_id | (op << 8) | (result << 16).
constexpr uint64_t hw_cache_config(uint32_t cache, uint32_t op, uint32_t result) noexcept {
    return static_cast<uint64_t>(cache)
         | (static_cast<uint64_t>(op)     << 8)
         | (static_cast<uint64_t>(result) << 16);
}

const PmuEventSpec kEventSpecs[] = {
    // LLC miss (read+miss).  Sample every 10K LLC misses → typically
    // 10-100 samples/sec on cache-bound workloads.
    {"pmu_llc",        PERF_TYPE_HW_CACHE,
     hw_cache_config(PERF_COUNT_HW_CACHE_LL,
                     PERF_COUNT_HW_CACHE_OP_READ,
                     PERF_COUNT_HW_CACHE_RESULT_MISS),
     10000, false, nullptr, "LLC-miss"},

    // Branch misprediction.  Sample every 10K mispredictions.
    {"pmu_branch",     PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_MISSES,
     10000, false, nullptr, "branch-miss"},

    // DTLB miss (read+miss).  Sample every 10K misses.
    {"pmu_dtlb",       PERF_TYPE_HW_CACHE,
     hw_cache_config(PERF_COUNT_HW_CACHE_DTLB,
                     PERF_COUNT_HW_CACHE_OP_READ,
                     PERF_COUNT_HW_CACHE_RESULT_MISS),
     10000, false, nullptr, "DTLB-miss"},

    // AMD IBS-Op (precise micro-op sample).  Dynamic type ID.
    {"pmu_ibs_op",     0, 0, 100000, true,
     "/sys/bus/event_source/devices/ibs_op/type", "IBS-op"},

    // AMD IBS-Fetch (precise instruction fetch).  Dynamic type ID.
    {"pmu_ibs_fetch",  0, 0, 100000, true,
     "/sys/bus/event_source/devices/ibs_fetch/type", "IBS-fetch"},

    // Major page fault (SW event).  Sample every fault.
    {"pmu_sw_pagefault_maj", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS_MAJ,
     1, false, nullptr, "major-pagefault"},

    // CPU migration (SW event).  Sample every migration.
    {"pmu_sw_cpu_migration", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CPU_MIGRATIONS,
     1, false, nullptr, "cpu-migration"},

    // Alignment fault (SW event).  Sample every fault.  Note:
    // alignment-fault SW counter only works on architectures that
    // generate them (some AArch64 configs); x86_64 returns 0 always.
    {"pmu_sw_alignment_fault", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_ALIGNMENT_FAULTS,
     1, false, nullptr, "alignment-fault"},
};
constexpr size_t kEventSpecCount = sizeof(kEventSpecs) / sizeof(kEventSpecs[0]);
static_assert(kEventSpecCount == 8,
    "Event spec table must match 8 SEC(\"perf_event\") programs in "
    "include/crucible/perf/bpf/pmu_sample.bpf.c");

[[nodiscard]] int libbpf_errno(const void* p, int fallback) noexcept {
    const long le = libbpf_get_error(p);
    return le ? static_cast<int>(-le) : fallback;
}

}  // namespace

// State CRTP-inherits NonMovable for the same exclusive-resource
// reason as SenseHub::State and SchedSwitch::State.  Three
// resource families: bpf_object, bpf_link[], perf_event FDs[],
// and the mmap.
struct PmuSample::State : crucible::safety::NonMovable<PmuSample::State> {
    struct bpf_object*                       obj = nullptr;
    std::inplace_vector<struct bpf_link*, 8> links{};
    // perf_event FDs we opened — kept open for the lifetime of the
    // attached BPF link (the link holds a reference but the FD must
    // not be close()d before the link is bpf_link__destroy()ed; we
    // own both until State::~State runs).
    std::inplace_vector<PerfFd, 8>           perf_fds{};

    // Single-set during Phase 7.  Same paired-set discipline as the
    // siblings — both has_value() iff Phase 7 succeeded.
    safety::WriteOnceNonNull<volatile uint8_t*> timeline_base{};
    safety::WriteOnce<size_t>                   timeline_len{};

    // Monotonic — only bumps on attach failures.
    safety::Monotonic<size_t>                   attach_fail_cnt{0};

    State() = default;

    ~State() {
        for (struct bpf_link* l : links) if (l != nullptr) bpf_link__destroy(l);
        for (PerfFd fd : perf_fds) {
            if (fd.value() >= 0) ::close(fd.value());
        }
        if (timeline_base.has_value()) {
            ::munmap(const_cast<uint8_t*>(timeline_base.get()),
                     timeline_len.get());
        }
        if (obj != nullptr) bpf_object__close(obj);
    }
};

PmuSample::PmuSample() noexcept = default;
PmuSample::PmuSample(PmuSample&&) noexcept = default;
PmuSample& PmuSample::operator=(PmuSample&&) noexcept = default;
PmuSample::~PmuSample() = default;

std::optional<PmuSample> PmuSample::load(::crucible::effects::Init) noexcept {
    install_libbpf_log_cb_once();

    const auto report = [](const char* why, int err = 0) {
        if (quiet()) return;
        if (err != 0) {
            std::fprintf(stderr,
                "[crucible::perf] pmu_sample unavailable: %s (%s)\n",
                why, std::strerror(err));
        } else {
            std::fprintf(stderr,
                "[crucible::perf] pmu_sample unavailable: %s\n", why);
        }
    };

    auto state = std::make_unique<State>();

    // ── 1. Parse the embedded ELF ──────────────────────────────────
    struct bpf_object_open_opts opts{};
    opts.sz          = sizeof(opts);
    opts.object_name = "crucible_pmu_sample";
    struct bpf_object* obj = bpf_object__open_mem(
        pmu_sample_bpf_bytecode,
        static_cast<size_t>(pmu_sample_bpf_bytecode_len),
        &opts);
    if (obj == nullptr || libbpf_get_error(obj) != 0) {
        const int e = libbpf_errno(obj, errno);
        state->obj = nullptr;
        report("bpf_object__open_mem failed (corrupt embedded bytecode — rebuild)", e);
        return std::nullopt;
    }
    state->obj = obj;

    // ── 2. Rewrite target_tgid in .rodata to our PID ───────────────
    if (struct bpf_map* rodata = find_rodata(state->obj); rodata != nullptr) {
        size_t vsz = 0;
        const void* current = bpf_map__initial_value(rodata, &vsz);
        if (current != nullptr && vsz >= sizeof(uint32_t)) {
            std::string rewritten(static_cast<const char*>(current), vsz);
            const Tgid     tgid     = current_tgid();
            const uint32_t tgid_raw = tgid.value();
            std::memcpy(rewritten.data(), &tgid_raw, sizeof(tgid_raw));
            (void)bpf_map__set_initial_value(rodata, rewritten.data(), vsz);
        }
    }

    // ── 3. Verify, JIT, allocate maps ──────────────────────────────
    //
    // No equivalent of disable_unavailable_programs() here — the
    // perf_event programs always load (the verifier doesn't care
    // whether the PMU is available; that's checked by perf_event_open
    // in Phase 5).
    if (const int err = bpf_object__load(state->obj); err != 0) {
        report("bpf_object__load failed (apply CAP_BPF+CAP_PERFMON; "
               "verifier rejected, missing CAP_BPF, or kernel too old)", -err);
        return std::nullopt;
    }

    // ── 4. perf_event_open + attach for each event spec ────────────
    //
    // Each event type gets ONE perf_event FD (cpu=-1, pid=getpid()
    // means "track this process across all CPUs the kernel
    // schedules it on"), then a single bpf_program__attach_perf_event
    // call.  Failures are bounded — non-AMD systems silently skip
    // IBS (kEventSpecCount-2 = 6 programs attached).  An ENOSYS or
    // EACCES on perf_event_open means the system has restrictive
    // perf_event_paranoid or no CAP_PERFMON; we still try the
    // SOFTWARE events which only need PERFMON_PARANOID <= 2.
    const Tgid     tgid     = current_tgid();
    const uint32_t tgid_raw = tgid.value();

    for (const auto& spec : kEventSpecs) {
        // Resolve dynamic perf_type if needed (IBS).
        uint32_t perf_type = spec.perf_type;
        if (spec.is_dynamic) {
            const int dyn = read_dynamic_pmu_type(spec.dynamic_path);
            if (dyn < 0) {
                state->attach_fail_cnt.bump();
                if (verbose()) {
                    std::fprintf(stderr,
                        "[crucible::perf] pmu_sample %s skipped "
                        "(dynamic PMU type not available — non-AMD?)\n",
                        spec.friendly_name);
                }
                continue;
            }
            perf_type = static_cast<uint32_t>(dyn);
        }

        // Find the BPF program by section name.
        struct bpf_program* prog =
            bpf_object__find_program_by_name(state->obj, spec.prog_name);
        if (prog == nullptr) {
            state->attach_fail_cnt.bump();
            if (verbose()) {
                std::fprintf(stderr,
                    "[crucible::perf] pmu_sample program '%s' not found "
                    "(bytecode/spec table out of sync — rebuild)\n",
                    spec.prog_name);
            }
            continue;
        }

        // Build perf_event_attr.  exclude_kernel=1: BPF program
        // already filters kernel IPs but eliminating them at the
        // perf layer saves an unnecessary BPF invocation.
        // sample_period: env-var override per category (HW/IBS/SW)
        // — see resolve_period() docblock.  Defaults to spec table.
        const uint64_t effective_period = resolve_period(
            perf_type, spec.is_dynamic, spec.sample_period);
        struct perf_event_attr attr{};
        attr.size           = sizeof(attr);
        attr.type           = perf_type;
        attr.config         = spec.perf_config;
        attr.sample_period  = effective_period;
        attr.exclude_kernel = 1;
        attr.exclude_hv     = 1;
        attr.disabled       = 0;

        // perf_event_open(attr, pid, cpu=-1, group_fd=-1, flags=0).
        // pid > 0 + cpu = -1 means "track this PID across all CPUs".
        const long fd_raw = perf_event_open_syscall(
            &attr, static_cast<pid_t>(tgid_raw),
            /*cpu=*/-1, /*group_fd=*/-1, /*flags=*/0);
        if (fd_raw < 0) {
            state->attach_fail_cnt.bump();
            if (verbose()) {
                std::fprintf(stderr,
                    "[crucible::perf] pmu_sample %s perf_event_open failed (%s)\n",
                    spec.friendly_name, std::strerror(errno));
            }
            continue;
        }
        const PerfFd perf_fd{static_cast<int>(fd_raw)};

        // Attach the BPF program to this perf_event.
        struct bpf_link* link =
            bpf_program__attach_perf_event(prog, perf_fd.value());
        const long lerr = libbpf_get_error(link);
        if (link == nullptr || lerr != 0) {
            ::close(perf_fd.value());
            state->attach_fail_cnt.bump();
            if (verbose()) {
                std::fprintf(stderr,
                    "[crucible::perf] pmu_sample %s attach failed (%s)\n",
                    spec.friendly_name,
                    std::strerror(lerr ? static_cast<int>(-lerr) : errno));
            }
            continue;
        }

        // inplace_vector caps at 8 — should never overflow since
        // kEventSpecCount == 8 — but be defensive.
        if (state->links.size() == state->links.capacity()) {
            bpf_link__destroy(link);
            ::close(perf_fd.value());
            state->attach_fail_cnt.bump();
            continue;
        }
        state->links.push_back(link);
        state->perf_fds.push_back(perf_fd);

        // Verbose attach log — one line per success, GAPS-004c-AUDIT.
        // Lets the user verify env-var sample-period overrides took
        // effect.  Only fires under CRUCIBLE_PERF_VERBOSE=1.
        if (verbose()) {
            std::fprintf(stderr,
                "[crucible::perf] pmu_sample attached %-15s "
                "type=%u config=0x%llx period=%llu\n",
                spec.friendly_name,
                perf_type,
                static_cast<unsigned long long>(spec.perf_config),
                static_cast<unsigned long long>(effective_period));
        }
    }

    if (state->links.empty()) {
        report("no perf_event programs attached (apply CAP_PERFMON; "
               "kernel.perf_event_paranoid > 2 blocks unprivileged use)");
        return std::nullopt;
    }

    // ── 5. mmap the pmu_sample_buf ─────────────────────────────────
    struct bpf_map* timeline_map =
        bpf_object__find_map_by_name(state->obj, "pmu_sample_buf");
    if (timeline_map == nullptr) {
        report("pmu_sample_buf map not found in object (bytecode/header out of sync — rebuild)");
        return std::nullopt;
    }
    const BpfFd timeline_fd{bpf_map__fd(timeline_map)};

    const long page_l = ::sysconf(_SC_PAGESIZE);
    if (page_l <= 0) {
        report("sysconf(_SC_PAGESIZE) failed (hardened sandbox blocking syscalls?)", errno);
        return std::nullopt;
    }
    const size_t page = static_cast<size_t>(page_l);
    // sizeof(PmuSampleHeader) + sizeof(events[PMU_SAMPLE_CAPACITY])
    //   = 64 + (32768 * 32) = 1,048,640 bytes
    // Page-rounded: ~1 MB exactly (mmap rounds to next page).
    const size_t bytes          = sizeof(PmuSampleHeader) +
                                  PMU_SAMPLE_CAPACITY * sizeof(PmuSampleEvent);
    const size_t mmap_len_bytes = (bytes + page - 1) & ~(page - 1);
    void* mmap_address = ::mmap(nullptr, mmap_len_bytes, PROT_READ, MAP_SHARED,
                                timeline_fd.value(), 0);
    if (mmap_address == MAP_FAILED) {
        report("mmap of pmu_sample_buf failed (apply CAP_BPF; "
               "BPF_F_MMAPABLE requires CAP_BPF or kernel ≥ 5.5)", errno);
        return std::nullopt;
    }

    // Paired single-set: same len-first, base-second discipline as
    // SenseHub/SchedSwitch — under semantic=enforce a hypothetical
    // contract violation on base.set() leaves a self-consistent
    // dtor view.
    state->timeline_len.set(mmap_len_bytes);
    state->timeline_base.set(static_cast<volatile uint8_t*>(mmap_address));

    // Surface partial-coverage warnings once per successful load.
    if (!quiet() && state->attach_fail_cnt.get() != 0) {
        std::fprintf(stderr,
            "[crucible::perf] pmu_sample partial: %zu of %zu programs failed to attach "
            "(set CRUCIBLE_PERF_VERBOSE=1 to see which)\n",
            state->attach_fail_cnt.get(), kEventSpecCount);
    }

    PmuSample h;
    h.state_ = std::move(state);
    return h;
}

safety::Borrowed<const PmuSampleEvent, PmuSample>
PmuSample::timeline_view() const noexcept {
    if (state_ == nullptr || !state_->timeline_base) {
        return safety::Borrowed<const PmuSampleEvent, PmuSample>{};
    }
    auto* base = state_->timeline_base.get();
    auto* events = reinterpret_cast<const PmuSampleEvent*>(
        const_cast<const uint8_t*>(base + sizeof(PmuSampleHeader)));
    return safety::Borrowed<const PmuSampleEvent, PmuSample>{
        events, PMU_SAMPLE_CAPACITY};
}

uint64_t PmuSample::timeline_write_index() const noexcept {
    if (state_ == nullptr || !state_->timeline_base) return 0;
    auto* base = state_->timeline_base.get();
    auto* hdr  = reinterpret_cast<const volatile PmuSampleHeader*>(base);
    return hdr->write_idx;
}

safety::Refined<safety::bounded_above<8>, std::size_t>
PmuSample::attached_programs() const noexcept {
    using R = safety::Refined<safety::bounded_above<8>, std::size_t>;
    return R{(state_ != nullptr) ? state_->links.size() : std::size_t{0}};
}

safety::Refined<safety::bounded_above<8>, std::size_t>
PmuSample::attach_failures() const noexcept {
    using R = safety::Refined<safety::bounded_above<8>, std::size_t>;
    return R{(state_ != nullptr) ? state_->attach_fail_cnt.get()
                                 : std::size_t{0}};
}

}  // namespace crucible::perf
