#include <crucible/perf/PmuSample.h>

#include <crucible/perf/detail/BpfHub.h>

#include <fixy/Tagged.h>
#include <foundation/Pinned.h>

#include <linux/perf_event.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <inplace_vector>
#include <memory>
#include <optional>
#include <span>

extern "C" {
extern const unsigned char pmu_sample_bpf_bytecode[];
extern const unsigned int pmu_sample_bpf_bytecode_len;
}

namespace crucible::perf {

namespace {

using ::crucible::perf::detail::verbose;

struct PmuSampleRingTag {};
using PmuSampleRing = detail::RingLayout<PmuSampleHeader, PmuSampleEvent, PMU_SAMPLE_CAPACITY>;

// A perf_event_open fd is not a libbpf map fd, even though both reduce to
// int.  The separate tag keeps the two distinguishable at the type level.
namespace local_source {
struct PerfEvent {};
}  // namespace local_source
using PerfFd = ::fixy::Tagged<int, local_source::PerfEvent>;
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
[[nodiscard]] long perf_event_open_syscall(perf_event_attr* attr, pid_t pid, int cpu, int group_fd,
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

// The perf event fd that each kept link reads.  A link must go before its
// fd, so the State declares this holder before its BPF object.  Members go
// in reverse order, and the object destroys its links before these fds
// close.
struct PerfFds : ::foundation::NonMovable<PerfFds> {
    std::inplace_vector<PerfFd, kEventSpecCount> fds{};

    PerfFds() noexcept = default;

    ~PerfFds() {
        for (const PerfFd fd : fds) {
            ::close(fd.value());  // SYSCALL-CAP-OK: closes the fds that a load under an Init context opened
        }
    }
};

}  // namespace

struct PmuSample::State : ::foundation::NonMovable<PmuSample::State> {
    // Each event spec settles once, so the link table holds one slot for
    // each spec, and settle() never sees an outcome past its bound.
    static constexpr int kMaxLinks = 8;
    static_assert(kEventSpecCount == static_cast<std::size_t>(kMaxLinks), "Give each event spec one link slot.");

    PerfFds perf_fds{};
    detail::BpfObject<kMaxLinks> object{};
    std::optional<detail::ReadOnlyMapping<PmuSampleRingTag>> timeline{};
};

PmuSample::PmuSample() noexcept = default;
PmuSample::PmuSample(PmuSample&&) noexcept = default;
PmuSample& PmuSample::operator=(PmuSample&&) noexcept = default;
PmuSample::~PmuSample() = default;

std::optional<PmuSample> PmuSample::load(::fixy::InitLoadCtx const& ctx) noexcept {
    constexpr const char* facade = "pmu_sample";
    // A perf_event program needs no tracepoint probe.  The verifier does
    // not care whether the PMU exists, and perf_event_open below reports an
    // unavailable one.
    const detail::LoadSpec spec{
        .facade = facade,
        .object_name = "crucible_pmu_sample",
        .bytecode = std::span{pmu_sample_bpf_bytecode, static_cast<std::size_t>(pmu_sample_bpf_bytecode_len)},
        .probe_tracepoints = false,
        .load_advice = "(apply CAP_BPF+CAP_PERFMON; verifier rejected, missing CAP_BPF, or kernel too old)",
        .attach_advice = "no perf_event programs attached (apply CAP_PERFMON; kernel.perf_event_paranoid > 2 "
                         "blocks unprivileged use)",
    };
    auto state = std::make_unique<State>();
    if (!state->object.open_and_load(spec)) return std::nullopt;

    // A failure below is per event type rather than fatal.  A machine without
    // the IBS driver has no dynamic type, and a restrictive
    // perf_event_paranoid setting blocks the hardware events while still
    // permitting the software ones.
    const uint32_t tgid = detail::current_tgid().value();

    for (const PmuEventSpec& event : kEventSpecs) {
        uint32_t perf_type = event.perf_type;
        if (event.is_dynamic) {
            const int dynamic_type = read_dynamic_pmu_type(event.dynamic_path);
            if (dynamic_type < 0) {
                state->object.settle(nullptr);
                if (verbose()) {
                    std::fprintf(stderr,
                                 "[crucible::perf] pmu_sample %s skipped "
                                 "(dynamic PMU type not available; not an AMD host?)\n",
                                 event.friendly_name);
                }
                continue;
            }
            perf_type = static_cast<uint32_t>(dynamic_type);
        }

        bpf_program* const prog = state->object.find_program(event.prog_name);
        if (prog == nullptr) {
            state->object.settle(nullptr);
            if (verbose()) {
                std::fprintf(stderr,
                             "[crucible::perf] pmu_sample program '%s' not found "
                             "(bytecode and spec table out of sync; rebuild)\n",
                             event.prog_name);
            }
            continue;
        }

        // The BPF program filters kernel addresses out anyway, but excluding
        // them at the perf layer saves the BPF invocation entirely.
        const uint64_t effective_period = resolve_period(perf_type, event.is_dynamic, event.sample_period);
        perf_event_attr attr{};
        attr.size = sizeof(attr);
        attr.type = perf_type;
        attr.config = event.perf_config;
        attr.sample_period = effective_period;
        attr.exclude_kernel = 1;
        attr.exclude_hv = 1;
        attr.disabled = 0;

        // A positive pid with cpu set to -1 tracks that process on every CPU
        // the kernel schedules it on.
        const long fd_raw = perf_event_open_syscall(&attr, static_cast<pid_t>(tgid),
                                                    /*cpu=*/-1, /*group_fd=*/-1, /*flags=*/0);
        if (fd_raw < 0) {
            state->object.settle(nullptr);
            if (verbose()) {
                std::fprintf(stderr, "[crucible::perf] pmu_sample %s perf_event_open failed (%s)\n",
                             event.friendly_name, std::strerror(errno));
            }
            continue;
        }
        const PerfFd perf_fd = ::fixy::mint_tagged<local_source::PerfEvent>(static_cast<int>(fd_raw));

        bpf_link* const link = bpf_program__attach_perf_event(prog, perf_fd.value());
        if (!state->object.settle(link)) {
            const long err = libbpf_get_error(link);
            ::close(perf_fd.value());  // SYSCALL-CAP-OK: closes the fd of an event whose attach failed
            if (verbose()) {
                std::fprintf(stderr, "[crucible::perf] pmu_sample %s attach failed (%s)\n", event.friendly_name,
                             std::strerror(err != 0 ? static_cast<int>(-err) : errno));
            }
            continue;
        }
        // One fd for each kept link, and the object keeps at most one link
        // for each of the kEventSpecCount specs, so the push has room.
        state->perf_fds.fds.push_back(perf_fd);

        if (verbose()) {
            std::fprintf(stderr,
                         "[crucible::perf] pmu_sample attached %-15s "
                         "type=%u config=0x%llx period=%llu\n",
                         event.friendly_name, perf_type, static_cast<unsigned long long>(event.perf_config),
                         static_cast<unsigned long long>(effective_period));
        }
    }

    if (!state->object.require_attached(spec)) return std::nullopt;
    state->timeline =
        detail::map_array<PmuSampleRingTag>(ctx, state->object, facade, "pmu_sample_buf", PmuSampleRing::bytes);
    if (!state->timeline) return std::nullopt;
    state->object.report_partial(facade);

    PmuSample hub;
    hub.state_ = std::move(state);
    return hub;
}

::fixy::Borrowed<const PmuSampleEvent, PmuSample> PmuSample::timeline_view() const noexcept {
    return state_ != nullptr ? PmuSampleRing::events<PmuSample>(state_->timeline)
                             : ::fixy::Borrowed<const PmuSampleEvent, PmuSample>{};
}

uint64_t PmuSample::timeline_write_index() const noexcept {
    return state_ != nullptr ? PmuSampleRing::write_index(state_->timeline) : 0;
}

::fixy::Refined<::fixy::bounded_above<8>, std::size_t> PmuSample::attached_programs() const noexcept {
    return detail::attached_programs(state_.get());
}

::fixy::Refined<::fixy::bounded_above<8>, std::size_t> PmuSample::attach_failures() const noexcept {
    return detail::attach_failures(state_.get());
}

PmuSample::Snapshot PmuSample::snapshot() const noexcept { return Snapshot{.samples = timeline_write_index()}; }

}  // namespace crucible::perf
