#include <crucible/perf/PmuSample.h>

#include <crucible/perf/detail/BpfHub.h>

#include <fixy/Core.h>
#include <fixy/Qtt.h>
#include <fixy/os/Environment.h>
#include <fixy/os/PerfEvent.h>
#include <fixy/os/Process.h>
#include <foundation/Pinned.h>
#include <foundation/core/Array.h>
#include <foundation/os/File.h>

#include <cstddef>
#include <cstdint>
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

// The class of the period of an event, and the environment variable that
// can override the period of each class.
enum class PeriodClass : uint8_t {
    hardware,
    software,
    dynamic,
};

// The period that the variable name of the environment gives, or fallback
// when the variable is absent, is no decimal number, or is zero.  The grammar
// of fixy::parse refuses a sign, so "-5" does not become a value near the
// maximum of the type, which would suppress sampling altogether.
[[nodiscard]] uint64_t env_period(::fixy::CText name, uint64_t fallback) noexcept {
    return ::fixy::env::read_variable(name).match(
        [fallback](::fixy::TextView text) noexcept {
            return ::fixy::parse<uint64_t>(text).match(
                [fallback](uint64_t period) noexcept { return period == 0 ? fallback : period; },
                [fallback](::fixy::ParseError) noexcept { return fallback; });
        },
        [fallback] noexcept { return fallback; });
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
[[nodiscard]] uint64_t resolve_period(PeriodClass period_class, uint64_t default_period) noexcept {
    switch (period_class) {
        case PeriodClass::dynamic:
            return period_ibs(default_period);
        case PeriodClass::software:
            return period_sw(default_period);
        case PeriodClass::hardware:
        default:
            return period_hw(default_period);
    }
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
    // The event of a fixed PMU.  A dynamic event takes its type from sysfs
    // at the load, and its config is zero.
    ::fixy::perf::Event event{};
    PeriodClass period_class = PeriodClass::hardware;
    uint64_t sample_period = 0;
    bool is_dynamic = false;
    // The sysfs file of the type id of a dynamic PMU, for a dynamic event.
    ::fixy::CText dynamic_path{};
    ::fixy::TextView friendly_name{};
};

// One row for each perf_event program in the BPF object.
constexpr std::size_t kEventSpecCount = 8;

constexpr ::foundation::core::FixedArray<PmuEventSpec, kEventSpecCount> kEventSpecs{
    PmuEventSpec{
        .prog_name = CRUCIBLE_PERF_FOREIGN_NAME("pmu_llc"),
        .event = ::fixy::perf::cache_read_miss(::fixy::perf::Cache::last_level),
        .sample_period = 10000,
        .friendly_name = "LLC-miss",
    },
    PmuEventSpec{
        .prog_name = CRUCIBLE_PERF_FOREIGN_NAME("pmu_branch"),
        .event = ::fixy::perf::hardware_event(::fixy::perf::HardwareEvent::branch_misses),
        .sample_period = 10000,
        .friendly_name = "branch-miss",
    },
    PmuEventSpec{
        .prog_name = CRUCIBLE_PERF_FOREIGN_NAME("pmu_dtlb"),
        .event = ::fixy::perf::cache_read_miss(::fixy::perf::Cache::data_tlb),
        .sample_period = 10000,
        .friendly_name = "DTLB-miss",
    },
    PmuEventSpec{
        .prog_name = CRUCIBLE_PERF_FOREIGN_NAME("pmu_ibs_op"),
        .period_class = PeriodClass::dynamic,
        .sample_period = 100000,
        .is_dynamic = true,
        .dynamic_path = "/sys/bus/event_source/devices/ibs_op/type",
        .friendly_name = "IBS-op",
    },
    PmuEventSpec{
        .prog_name = CRUCIBLE_PERF_FOREIGN_NAME("pmu_ibs_fetch"),
        .period_class = PeriodClass::dynamic,
        .sample_period = 100000,
        .is_dynamic = true,
        .dynamic_path = "/sys/bus/event_source/devices/ibs_fetch/type",
        .friendly_name = "IBS-fetch",
    },
    PmuEventSpec{
        .prog_name = CRUCIBLE_PERF_FOREIGN_NAME("pmu_sw_pagefault_maj"),
        .event = ::fixy::perf::software_event(::fixy::perf::SoftwareEvent::major_page_faults),
        .period_class = PeriodClass::software,
        .sample_period = 1,
        .friendly_name = "major-pagefault",
    },
    PmuEventSpec{
        .prog_name = CRUCIBLE_PERF_FOREIGN_NAME("pmu_sw_cpu_migration"),
        .event = ::fixy::perf::software_event(::fixy::perf::SoftwareEvent::cpu_migrations),
        .period_class = PeriodClass::software,
        .sample_period = 1,
        .friendly_name = "cpu-migration",
    },
    // The alignment-fault counter reports events only on an architecture
    // that raises them.  x86-64 always reads back zero.
    PmuEventSpec{
        .prog_name = CRUCIBLE_PERF_FOREIGN_NAME("pmu_sw_alignment_fault"),
        .event = ::fixy::perf::software_event(::fixy::perf::SoftwareEvent::alignment_faults),
        .period_class = PeriodClass::software,
        .sample_period = 1,
        .friendly_name = "alignment-fault",
    },
};

}  // namespace

// The link of each event owns the descriptor of the event: libbpf closes the
// descriptor when the object destroys the link.  So the state holds no
// descriptor of its own.
struct PmuSample::State : ::foundation::NonMovable<PmuSample::State> {
    // Each event spec settles once, so the link table holds one slot for
    // each spec, and settle() never sees an outcome past its bound.
    static constexpr int kMaxLinks = 8;
    static_assert(kEventSpecCount == static_cast<std::size_t>(kMaxLinks), "Give each event spec one link slot.");

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
    if (!loaded.object.open_and_load(ctx, spec)) return false;

    // A failure below is per event type rather than fatal.  A machine without
    // the IBS driver has no dynamic type, and a restrictive
    // perf_event_paranoid setting blocks the hardware events while still
    // permitting the software ones.
    ::fixy::process::ProcessId const process = ::fixy::process::current_process(ctx);

    for (const PmuEventSpec& event : kEventSpecs) {
        ::fixy::perf::Event perf_event = event.event;
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
                perf_event = ::fixy::perf::Event{.type = type, .config = 0};
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

        // The BPF program filters kernel addresses out anyway, but the door
        // opens each event for user space only, which saves the BPF
        // invocation entirely.  The event follows the first thread of the
        // process on every CPU that the kernel schedules it on.
        const uint64_t effective_period = resolve_period(event.period_class, event.sample_period);
        ::fixy::fs::OwnedFd descriptor =
            ::fixy::perf::mint_perf_event(ctx, perf_event, effective_period, process)
                .match(
                    [](::fixy::Linear<::fixy::fs::OwnedFd> opened) noexcept -> ::fixy::fs::OwnedFd {
                        return std::move(opened).consume();
                    },
                    [&event](::foundation::os::Errno error) noexcept -> ::fixy::fs::OwnedFd {
                        if (verbose()) {
                            ::fixy::report(::fixy::Sink::Err,
                                           "[crucible::perf] pmu_sample {} perf_event_open failed (error {})\n",
                                           event.friendly_name, std::to_underlying(error));
                        }
                        return ::fixy::fs::OwnedFd{};
                    });
        if (!descriptor.is_open()) {
            loaded.object.settle(nullptr);
            continue;
        }

        bpf_link* const link = bpf_program__attach_perf_event(prog, descriptor.get());
        const int error = detail::libbpf_errno(link);
        // A live link owns the descriptor: libbpf closes it when the object
        // destroys the link, also when settle() refuses the link.  The handle
        // then gives the descriptor up.  The handle of a failed attach closes
        // the descriptor at the end of the iteration.
        if (link != nullptr && error == 0) {
            static_cast<void>(descriptor.release());
        }
        if (!loaded.object.settle(link)) {
            if (verbose()) {
                ::fixy::report(::fixy::Sink::Err, "[crucible::perf] pmu_sample {} attach failed (error {})\n",
                               event.friendly_name, error);
            }
            continue;
        }

        if (verbose()) {
            ::fixy::report(::fixy::Sink::Err, "[crucible::perf] pmu_sample attached {} type={} config={} period={}\n",
                           event.friendly_name, perf_event.type, perf_event.config, effective_period);
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
