#include <crucible/perf/PmuSample.h>

#include <crucible/perf/detail/BpfHub.h>

#include <fixy/Core.h>
#include <fixy/Tagged.h>
#include <foundation/Pinned.h>
#include <foundation/core/Array.h>
#include <foundation/os/File.h>

#include <linux/perf_event.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <utility>

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

// The characters of text before its first newline.
[[nodiscard]] ::fixy::TextView first_line(::fixy::TextView text) noexcept {
    std::size_t const newline = ::fixy::find(text, '\n').value_or(text.size());
    return text.window(0, newline).value_or(::fixy::TextView{});
}

// An IBS event carries a dynamic PMU type id that the kernel allocates at
// boot when the driver registers, and publishes through sysfs.  A system
// without that driver has no such file, and the type then has no value.
[[nodiscard]] ::fixy::Option<uint32_t> read_dynamic_pmu_type(::fixy::CText path) noexcept {
    ::fixy::FixedText<32> text{};
    if (::foundation::os::read_file(path, text).is_err()) return ::fixy::none;
    return ::fixy::parse<uint32_t>(first_line(text.view())).ok();
}

struct PmuEventSpec {
    detail::ForeignName prog_name{};
    uint32_t perf_type = 0;
    uint64_t perf_config = 0;
    uint64_t sample_period = 0;
    bool is_dynamic = false;
    // The sysfs file of the type id of a dynamic PMU, for a dynamic event.
    ::fixy::CText dynamic_path{};
    ::fixy::TextView friendly_name{};
};

// The kernel packs a hardware-cache event descriptor into one config word.
constexpr uint64_t hw_cache_config(uint32_t cache, uint32_t op, uint32_t result) noexcept {
    return static_cast<uint64_t>(cache) | (static_cast<uint64_t>(op) << 8) | (static_cast<uint64_t>(result) << 16);
}

// One row for each perf_event program in the BPF object.
constexpr std::size_t kEventSpecCount = 8;

constexpr ::foundation::core::FixedArray<PmuEventSpec, kEventSpecCount> kEventSpecs{
    PmuEventSpec{
        .prog_name = CRUCIBLE_PERF_FOREIGN_NAME("pmu_llc"),
        .perf_type = PERF_TYPE_HW_CACHE,
        .perf_config =
            hw_cache_config(PERF_COUNT_HW_CACHE_LL, PERF_COUNT_HW_CACHE_OP_READ, PERF_COUNT_HW_CACHE_RESULT_MISS),
        .sample_period = 10000,
        .friendly_name = "LLC-miss",
    },
    PmuEventSpec{
        .prog_name = CRUCIBLE_PERF_FOREIGN_NAME("pmu_branch"),
        .perf_type = PERF_TYPE_HARDWARE,
        .perf_config = PERF_COUNT_HW_BRANCH_MISSES,
        .sample_period = 10000,
        .friendly_name = "branch-miss",
    },
    PmuEventSpec{
        .prog_name = CRUCIBLE_PERF_FOREIGN_NAME("pmu_dtlb"),
        .perf_type = PERF_TYPE_HW_CACHE,
        .perf_config =
            hw_cache_config(PERF_COUNT_HW_CACHE_DTLB, PERF_COUNT_HW_CACHE_OP_READ, PERF_COUNT_HW_CACHE_RESULT_MISS),
        .sample_period = 10000,
        .friendly_name = "DTLB-miss",
    },
    PmuEventSpec{
        .prog_name = CRUCIBLE_PERF_FOREIGN_NAME("pmu_ibs_op"),
        .sample_period = 100000,
        .is_dynamic = true,
        .dynamic_path = "/sys/bus/event_source/devices/ibs_op/type",
        .friendly_name = "IBS-op",
    },
    PmuEventSpec{
        .prog_name = CRUCIBLE_PERF_FOREIGN_NAME("pmu_ibs_fetch"),
        .sample_period = 100000,
        .is_dynamic = true,
        .dynamic_path = "/sys/bus/event_source/devices/ibs_fetch/type",
        .friendly_name = "IBS-fetch",
    },
    PmuEventSpec{
        .prog_name = CRUCIBLE_PERF_FOREIGN_NAME("pmu_sw_pagefault_maj"),
        .perf_type = PERF_TYPE_SOFTWARE,
        .perf_config = PERF_COUNT_SW_PAGE_FAULTS_MAJ,
        .sample_period = 1,
        .friendly_name = "major-pagefault",
    },
    PmuEventSpec{
        .prog_name = CRUCIBLE_PERF_FOREIGN_NAME("pmu_sw_cpu_migration"),
        .perf_type = PERF_TYPE_SOFTWARE,
        .perf_config = PERF_COUNT_SW_CPU_MIGRATIONS,
        .sample_period = 1,
        .friendly_name = "cpu-migration",
    },
    // The alignment-fault counter reports events only on an architecture
    // that raises them.  x86-64 always reads back zero.
    PmuEventSpec{
        .prog_name = CRUCIBLE_PERF_FOREIGN_NAME("pmu_sw_alignment_fault"),
        .perf_type = PERF_TYPE_SOFTWARE,
        .perf_config = PERF_COUNT_SW_ALIGNMENT_FAULTS,
        .sample_period = 1,
        .friendly_name = "alignment-fault",
    },
};

// The perf event fd that each kept link reads.  A link must go before its
// fd, so the State declares this holder before its BPF object.  Members go
// in reverse order, and the object destroys its links before these fds
// close.
struct PerfFds : ::foundation::NonMovable<PerfFds> {
    ::fixy::FixedList<PerfFd, kEventSpecCount> fds{};

    PerfFds() noexcept = default;

    ~PerfFds() {
        for (const PerfFd fd : fds) {
            ::close(fd.value());
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
    ::fixy::Option<detail::ReadOnlyMapping<PmuSampleRingTag>> timeline{};
};

PmuSample::PmuSample(::fixy::Box<State> state) noexcept
    : state_{::fixy::Option<::fixy::Box<State>>::some(std::move(state))} {}
PmuSample::PmuSample(PmuSample&&) noexcept = default;
PmuSample& PmuSample::operator=(PmuSample&&) noexcept = default;
PmuSample::~PmuSample() = default;

namespace {

// Loads the object, opens one perf event for each spec, attaches the
// program of each event, and maps the sample ring.  False means that the
// facade is unavailable, and the reason is already reported.
[[nodiscard]] bool fill_state(detail::FacadeDoor::State<PmuSample>& loaded, ::fixy::InitLoadCtx const& ctx) noexcept {
    constexpr detail::ForeignName facade = CRUCIBLE_PERF_FOREIGN_NAME("pmu_sample");
    // A perf_event program needs no tracepoint probe.  The verifier does
    // not care whether the PMU exists, and perf_event_open below reports an
    // unavailable one.
    const detail::LoadSpec spec{
        .facade = facade,
        .object_name = CRUCIBLE_PERF_FOREIGN_NAME("crucible_pmu_sample"),
        .bytecode = std::span{pmu_sample_bpf_bytecode, static_cast<std::size_t>(pmu_sample_bpf_bytecode_len)},
        .probe_tracepoints = false,
        .load_advice = "(apply CAP_BPF+CAP_PERFMON; verifier rejected, missing CAP_BPF, or kernel too old)",
        .attach_advice = "no perf_event programs attached (apply CAP_PERFMON; kernel.perf_event_paranoid > 2 "
                         "blocks unprivileged use)",
    };
    if (!loaded.object.open_and_load(spec)) return false;

    // A failure below is per event type rather than fatal.  A machine without
    // the IBS driver has no dynamic type, and a restrictive
    // perf_event_paranoid setting blocks the hardware events while still
    // permitting the software ones.
    const uint32_t tgid = detail::current_tgid().value();

    for (const PmuEventSpec& event : kEventSpecs) {
        uint32_t perf_type = event.perf_type;
        if (event.is_dynamic) {
            ::fixy::Option<uint32_t> const dynamic_type = read_dynamic_pmu_type(event.dynamic_path);
            if (dynamic_type.is_none()) {
                loaded.object.settle(nullptr);
                if (verbose()) {
                    ::fixy::report(::fixy::Sink::Err,
                                   "[crucible::perf] pmu_sample {} skipped "
                                   "(dynamic PMU type not available; not an AMD host?)\n",
                                   event.friendly_name);
                }
                continue;
            }
            for (uint32_t const type : dynamic_type) {
                perf_type = type;
            }
        }

        bpf_program* const prog = loaded.object.find_program(event.prog_name.abi);
        if (prog == nullptr) {
            loaded.object.settle(nullptr);
            if (verbose()) {
                ::fixy::report(::fixy::Sink::Err,
                               "[crucible::perf] pmu_sample program '{}' not found "
                               "(bytecode and spec table out of sync; rebuild)\n",
                               event.prog_name.text);
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
            loaded.object.settle(nullptr);
            if (verbose()) {
                ::fixy::report(::fixy::Sink::Err, "[crucible::perf] pmu_sample {} perf_event_open failed (error {})\n",
                               event.friendly_name, errno);
            }
            continue;
        }
        const PerfFd perf_fd = ::fixy::mint_tagged<local_source::PerfEvent>(static_cast<int>(fd_raw));

        bpf_link* const link = bpf_program__attach_perf_event(prog, perf_fd.value());
        if (!loaded.object.settle(link)) {
            const long err = libbpf_get_error(link);
            // This owner closes the descriptor of the event at the end of
            // the block.
            PerfFds rejected{};
            static_cast<void>(rejected.fds.push(perf_fd).expect("an empty owner has room for one descriptor"));
            if (verbose()) {
                ::fixy::report(::fixy::Sink::Err, "[crucible::perf] pmu_sample {} attach failed (error {})\n",
                               event.friendly_name, err != 0 ? static_cast<int>(-err) : errno);
            }
            continue;
        }
        // One fd for each kept link, and the object keeps at most one link
        // for each of the kEventSpecCount specs, so the push has room.
        static_cast<void>(loaded.perf_fds.fds.push(perf_fd).expect("each spec keeps at most one descriptor"));

        if (verbose()) {
            ::fixy::report(::fixy::Sink::Err, "[crucible::perf] pmu_sample attached {} type={} config={} period={}\n",
                           event.friendly_name, perf_type, event.perf_config, effective_period);
        }
    }

    if (!loaded.object.require_attached(spec)) return false;
    loaded.timeline = detail::map_array<PmuSampleRingTag>(
        ctx, loaded.object, facade.text, CRUCIBLE_PERF_FOREIGN_NAME("pmu_sample_buf"), PmuSampleRing::bytes);
    if (loaded.timeline.is_none()) return false;
    loaded.object.report_partial(facade.text);
    return true;
}

}  // namespace

::fixy::Option<PmuSample> load_pmu_sample(::fixy::InitLoadCtx const& ctx) noexcept {
    return detail::FacadeDoor::load<PmuSample>(
        ctx, [&ctx](detail::FacadeDoor::State<PmuSample>& state) noexcept { return fill_state(state, ctx); });
}

::fixy::Borrowed<const PmuSampleEvent, PmuSample> PmuSample::timeline_view() const noexcept {
    return detail::read_held(
        state_, ::fixy::Borrowed<const PmuSampleEvent, PmuSample>{},
        [](State const& state) noexcept { return PmuSampleRing::events<PmuSample>(state.timeline); });
}

uint64_t PmuSample::timeline_write_index() const noexcept {
    return detail::read_held(state_, uint64_t{0},
                             [](State const& state) noexcept { return PmuSampleRing::write_index(state.timeline); });
}

::fixy::Refined<::fixy::bounded_above<8>, std::size_t> PmuSample::attached_programs() const noexcept {
    return detail::attached_programs(state_);
}

::fixy::Refined<::fixy::bounded_above<8>, std::size_t> PmuSample::attach_failures() const noexcept {
    return detail::attach_failures(state_);
}

PmuSample::Snapshot PmuSample::snapshot() const noexcept { return Snapshot{.samples = timeline_write_index()}; }

}  // namespace crucible::perf
