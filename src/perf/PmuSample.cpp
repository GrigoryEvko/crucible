#include <crucible/perf/PmuSample.h>

#include <crucible/perf/detail/BpfLoader.h>

#include <crucible/safety/_Mutation.h>
#include <crucible/safety/_OwnedMmap.h>
#include <crucible/safety/_Pinned.h>

#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>

#include <bit>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>

#include <inplace_vector>
#include <optional>

extern "C" {
extern const unsigned char pmu_sample_bpf_bytecode[];
extern const unsigned int pmu_sample_bpf_bytecode_len;
}

namespace crucible::perf {

namespace {

namespace source = ::crucible::perf::detail::source;
using ::crucible::perf::detail::Tgid;
using ::crucible::perf::detail::Tid;
using ::crucible::perf::detail::Fd;
using ::crucible::perf::detail::current_tgid;
using ::crucible::perf::detail::find_rodata;
using ::crucible::perf::detail::disable_unavailable_programs;
using ::crucible::perf::detail::libbpf_errno;
using ::crucible::perf::detail::install_libbpf_log_cb_once;
using ::crucible::perf::detail::quiet;
using ::crucible::perf::detail::verbose;

// A perf_event_open fd is not a libbpf map fd, even though both reduce to
// int.  The separate tag keeps the two distinguishable at the type level.
namespace local_source {
struct PerfEvent {};
}  // namespace local_source
using PerfFd = ::crucible::safety::Tagged<int, local_source::PerfEvent>;
static_assert(sizeof(PerfFd) == sizeof(int));

// strtoull accepts a leading '-' and returns the negation in unsigned
// arithmetic, so "-5" parses to a value near the maximum of the type.  As a
// sample period that suppresses sampling altogether, which is why the leading
// '-' is rejected outright and the fallback is used instead.
[[nodiscard]] uint64_t env_period(const char* name, uint64_t fallback) noexcept {
    const char* v = std::getenv(name);
    if (v == nullptr || v[0] == '\0' || v[0] == '-') return fallback;
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(v, &end, 10);
    if (end == v || parsed == 0) return fallback;
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
[[nodiscard]] uint64_t resolve_period(uint32_t perf_type, bool is_dynamic, uint64_t default_period) noexcept {
    if (is_dynamic) return period_ibs(default_period);
    if (perf_type == PERF_TYPE_SOFTWARE) return period_sw(default_period);
    return period_hw(default_period);
}

// glibc exposes no perf_event_open wrapper, so the call goes through the raw
// syscall entry point.
[[nodiscard]] long perf_event_open_syscall(struct perf_event_attr* attr, pid_t pid, int cpu, int group_fd,
                                           unsigned long flags) noexcept {
    return ::syscall(SYS_perf_event_open, attr, pid, cpu, group_fd, flags);
}

// An IBS event carries a dynamic PMU type id that the kernel allocates at
// boot when the driver registers, and publishes through sysfs.  A system
// without that driver has no such file, which reads back as -1.
[[nodiscard]] int read_dynamic_pmu_type(const char* path) noexcept {
    std::ifstream file(path);
    if (!file.is_open()) return -1;
    int type = -1;
    file >> type;
    return file.fail() ? -1 : type;
}

struct PmuEventSpec {
    const char* prog_name;
    uint32_t perf_type;
    uint64_t perf_config;
    uint64_t sample_period;
    bool is_dynamic;
    const char* dynamic_path;
    const char* friendly_name;
};

// The kernel packs a hardware-cache event descriptor into one config word.
constexpr uint64_t hw_cache_config(uint32_t cache, uint32_t op, uint32_t result) noexcept {
    return static_cast<uint64_t>(cache) | (static_cast<uint64_t>(op) << 8) | (static_cast<uint64_t>(result) << 16);
}

const PmuEventSpec kEventSpecs[] = {
    {"pmu_llc", PERF_TYPE_HW_CACHE,
     hw_cache_config(PERF_COUNT_HW_CACHE_LL, PERF_COUNT_HW_CACHE_OP_READ, PERF_COUNT_HW_CACHE_RESULT_MISS), 10000,
     false, nullptr, "LLC-miss"},

    {"pmu_branch", PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_MISSES, 10000, false, nullptr, "branch-miss"},

    {"pmu_dtlb", PERF_TYPE_HW_CACHE,
     hw_cache_config(PERF_COUNT_HW_CACHE_DTLB, PERF_COUNT_HW_CACHE_OP_READ, PERF_COUNT_HW_CACHE_RESULT_MISS), 10000,
     false, nullptr, "DTLB-miss"},

    {"pmu_ibs_op", 0, 0, 100000, true, "/sys/bus/event_source/devices/ibs_op/type", "IBS-op"},

    {"pmu_ibs_fetch", 0, 0, 100000, true, "/sys/bus/event_source/devices/ibs_fetch/type", "IBS-fetch"},

    {"pmu_sw_pagefault_maj", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS_MAJ, 1, false, nullptr, "major-pagefault"},

    {"pmu_sw_cpu_migration", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CPU_MIGRATIONS, 1, false, nullptr, "cpu-migration"},

    // The alignment-fault counter reports events only on an architecture
    // that raises them.  x86-64 always reads back zero.
    {"pmu_sw_alignment_fault", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_ALIGNMENT_FAULTS, 1, false, nullptr,
     "alignment-fault"},
};
constexpr size_t kEventSpecCount = sizeof(kEventSpecs) / sizeof(kEventSpecs[0]);
static_assert(kEventSpecCount == 8, "Event spec table must hold one row per perf_event program in the BPF object");

}  // namespace

struct PmuSample::State : crucible::safety::NonMovable<PmuSample::State> {
    struct bpf_object* obj = nullptr;
    std::inplace_vector<struct bpf_link*, 8> links{};
    // A perf_event fd must stay open until its link is destroyed, so the
    // destructor below destroys every link before closing any fd.
    std::inplace_vector<PerfFd, 8> perf_fds{};

    // The distinct phantom tag makes one facade's ring buffer mapping
    // unusable as another facade's mapping at compile time.  The protection
    // and sharing types are residency metadata that the mapping wrapper
    // never interprets.
    struct PmuSampleRingbufTag {};
    struct ReadOnlyProt {};
    struct SharedShare {};
    using TimelineMmap = ::crucible::safety::OwnedMmap<PmuSampleRingbufTag, ReadOnlyProt, SharedShare>;
    std::optional<TimelineMmap> timeline_mmap{};

    safety::Monotonic<size_t> attach_fail_cnt{0};

    State() = default;

    ~State() {
        for (struct bpf_link* l : links)
            if (l != nullptr) bpf_link__destroy(l);
        for (PerfFd fd : perf_fds) {
            if (fd.value() >= 0) ::close(fd.value());
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
            std::fprintf(stderr, "[crucible::perf] pmu_sample unavailable: %s (%s)\n", why, std::strerror(err));
        } else {
            std::fprintf(stderr, "[crucible::perf] pmu_sample unavailable: %s\n", why);
        }
    };

    auto state = std::make_unique<State>();

    struct bpf_object_open_opts opts{};
    opts.sz = sizeof(opts);
    opts.object_name = "crucible_pmu_sample";
    struct bpf_object* obj =
        bpf_object__open_mem(pmu_sample_bpf_bytecode, static_cast<size_t>(pmu_sample_bpf_bytecode_len), &opts);
    if (obj == nullptr || libbpf_get_error(obj) != 0) {
        const int e = libbpf_errno(obj, errno);
        state->obj = nullptr;
        report("bpf_object__open_mem failed (corrupt embedded bytecode — rebuild)", e);
        return std::nullopt;
    }
    state->obj = obj;

    if (struct bpf_map* rodata = find_rodata(state->obj); rodata != nullptr) {
        size_t vsz = 0;
        const void* current = bpf_map__initial_value(rodata, &vsz);
        if (current != nullptr && vsz >= sizeof(uint32_t)) {
            std::string rewritten(static_cast<const char*>(current), vsz);
            const Tgid tgid = current_tgid();
            const uint32_t tgid_raw = tgid.value();
            std::memcpy(rewritten.data(), &tgid_raw, sizeof(tgid_raw));
            (void)bpf_map__set_initial_value(rodata, rewritten.data(), vsz);
        }
    }

    // A perf_event program needs no availability pre-check.  The verifier
    // does not care whether the PMU exists.  perf_event_open below is what
    // reports an unavailable one.
    if (const int err = bpf_object__load(state->obj); err != 0) {
        report("bpf_object__load failed (apply CAP_BPF+CAP_PERFMON; "
               "verifier rejected, missing CAP_BPF, or kernel too old)",
               -err);
        return std::nullopt;
    }

    // A failure below is per event type rather than fatal.  A machine without
    // the IBS driver has no dynamic type, and a restrictive
    // perf_event_paranoid setting blocks the hardware events while still
    // permitting the software ones.
    const Tgid tgid = current_tgid();
    const uint32_t tgid_raw = tgid.value();

    for (const auto& spec : kEventSpecs) {
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

        struct bpf_program* prog = bpf_object__find_program_by_name(state->obj, spec.prog_name);
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

        // The BPF program filters kernel addresses out anyway, but excluding
        // them at the perf layer saves the BPF invocation entirely.
        const uint64_t effective_period = resolve_period(perf_type, spec.is_dynamic, spec.sample_period);
        struct perf_event_attr attr{};
        attr.size = sizeof(attr);
        attr.type = perf_type;
        attr.config = spec.perf_config;
        attr.sample_period = effective_period;
        attr.exclude_kernel = 1;
        attr.exclude_hv = 1;
        attr.disabled = 0;

        // A positive pid with cpu set to -1 tracks that process on every CPU
        // the kernel schedules it on.
        const long fd_raw = perf_event_open_syscall(&attr, static_cast<pid_t>(tgid_raw),
                                                    /*cpu=*/-1, /*group_fd=*/-1, /*flags=*/0);
        if (fd_raw < 0) {
            state->attach_fail_cnt.bump();
            if (verbose()) {
                std::fprintf(stderr, "[crucible::perf] pmu_sample %s perf_event_open failed (%s)\n", spec.friendly_name,
                             std::strerror(errno));
            }
            continue;
        }
        const PerfFd perf_fd{static_cast<int>(fd_raw)};

        struct bpf_link* link = bpf_program__attach_perf_event(prog, perf_fd.value());
        const long lerr = libbpf_get_error(link);
        if (link == nullptr || lerr != 0) {
            ::close(perf_fd.value());
            state->attach_fail_cnt.bump();
            if (verbose()) {
                std::fprintf(stderr, "[crucible::perf] pmu_sample %s attach failed (%s)\n", spec.friendly_name,
                             std::strerror(lerr ? static_cast<int>(-lerr) : errno));
            }
            continue;
        }

        if (state->links.size() == state->links.capacity()) {
            bpf_link__destroy(link);
            ::close(perf_fd.value());
            state->attach_fail_cnt.bump();
            continue;
        }
        state->links.push_back(link);
        state->perf_fds.push_back(perf_fd);

        if (verbose()) {
            std::fprintf(stderr,
                         "[crucible::perf] pmu_sample attached %-15s "
                         "type=%u config=0x%llx period=%llu\n",
                         spec.friendly_name, perf_type, static_cast<unsigned long long>(spec.perf_config),
                         static_cast<unsigned long long>(effective_period));
        }
    }

    if (state->links.empty()) {
        report("no perf_event programs attached (apply CAP_PERFMON; "
               "kernel.perf_event_paranoid > 2 blocks unprivileged use)");
        return std::nullopt;
    }

    struct bpf_map* timeline_map = bpf_object__find_map_by_name(state->obj, "pmu_sample_buf");
    if (timeline_map == nullptr) {
        report("pmu_sample_buf map not found in object (bytecode/header out of sync — rebuild)");
        return std::nullopt;
    }
    const Fd timeline_fd{bpf_map__fd(timeline_map)};

    const long page_l = ::sysconf(_SC_PAGESIZE);
    if (page_l <= 0) {
        report("sysconf(_SC_PAGESIZE) failed (hardened sandbox blocking syscalls?)", errno);
        return std::nullopt;
    }
    const size_t page = static_cast<size_t>(page_l);
    const size_t bytes = sizeof(PmuSampleHeader) + PMU_SAMPLE_CAPACITY * sizeof(PmuSampleEvent);
    const size_t mmap_len_bytes = (bytes + page - 1) & ~(page - 1);
    void* mmap_address = ::mmap(nullptr, mmap_len_bytes, PROT_READ, MAP_SHARED, timeline_fd.value(), 0);
    if (mmap_address == MAP_FAILED) {
        report("mmap of pmu_sample_buf failed (apply CAP_BPF; "
               "BPF_F_MMAPABLE requires CAP_BPF or kernel ≥ 5.5)",
               errno);
        return std::nullopt;
    }

    state->timeline_mmap.emplace(mmap_address, mmap_len_bytes);

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

safety::Borrowed<const PmuSampleEvent, PmuSample> PmuSample::timeline_view() const noexcept {
    if (state_ == nullptr || !state_->timeline_mmap) {
        return safety::Borrowed<const PmuSampleEvent, PmuSample>{};
    }
    auto* base = std::bit_cast<volatile uint8_t*>(state_->timeline_mmap->data());
    // The mapping is untyped byte storage, so start_lifetime_as_array begins
    // the typed array lifetime inside it.  The bit_cast drops volatile, which
    // is well defined at runtime and forbidden only in a constant expression.
    // The element type stays non-const: the const-void* overload already
    // returns a const pointer, and a const element type makes libstdc++ emit
    // an asm clobber that writes through a const-qualified location.
    auto* events = std::start_lifetime_as_array<PmuSampleEvent>(
        std::bit_cast<const uint8_t*>(base + sizeof(PmuSampleHeader)), PMU_SAMPLE_CAPACITY);
    return safety::Borrowed<const PmuSampleEvent, PmuSample>{events, PMU_SAMPLE_CAPACITY};
}

uint64_t PmuSample::timeline_write_index() const noexcept {
    if (state_ == nullptr || !state_->timeline_mmap) return 0;
    auto* base = std::bit_cast<volatile uint8_t*>(state_->timeline_mmap->data());
    // The added const selects the overload taking const volatile void*, which
    // returns a const volatile pointer to the header.
    const volatile uint8_t* qbase = base;
    auto* hdr = std::start_lifetime_as<PmuSampleHeader>(qbase);
    return hdr->write_idx;
}

safety::Refined<safety::bounded_above<8>, std::size_t> PmuSample::attached_programs() const noexcept {
    using R = safety::Refined<safety::bounded_above<8>, std::size_t>;
    return R{(state_ != nullptr) ? state_->links.size() : std::size_t{0}};
}

safety::Refined<safety::bounded_above<8>, std::size_t> PmuSample::attach_failures() const noexcept {
    using R = safety::Refined<safety::bounded_above<8>, std::size_t>;
    return R{(state_ != nullptr) ? state_->attach_fail_cnt.get() : std::size_t{0}};
}

PmuSample::Snapshot PmuSample::snapshot() const noexcept { return Snapshot{.samples = timeline_write_index()}; }

}  // namespace crucible::perf
