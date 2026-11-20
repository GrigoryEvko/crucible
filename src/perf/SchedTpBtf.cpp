#include <crucible/perf/SchedTpBtf.h>

#include <crucible/perf/detail/BpfHub.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace crucible::perf {

// The tag of the mapping of the facade.  The mapping takes the singleton
// root of the tag, so the tag has a stable identity and external linkage.
struct SchedTpBtfRingTag {};

struct SchedTpBtf::State : detail::RingState<SchedTpBtfRingTag, TimelineSchedEvent> {};

SchedTpBtf::SchedTpBtf(::fixy::Box<State> state) noexcept
    : state_{::fixy::Option<::fixy::Box<State>>::some(std::move(state))} {}
SchedTpBtf::SchedTpBtf(SchedTpBtf&&) noexcept = default;
SchedTpBtf& SchedTpBtf::operator=(SchedTpBtf&&) noexcept = default;
SchedTpBtf::~SchedTpBtf() = default;

::fixy::Option<SchedTpBtf> load_sched_tp_btf(::fixy::InitLoadCtx const& ctx) noexcept {
    const detail::RingSpec spec{
        .load = {.facade = CRUCIBLE_PERF_FOREIGN_NAME("sched_tp_btf"),
                 .object_name = CRUCIBLE_PERF_FOREIGN_NAME("crucible_sched_tp_btf"),
                 .bytecode = detail::sched_tp_btf_bpf_object(),
                 .probe_tracepoints = false,
                 .load_advice = "(apply CAP_BPF+CAP_PERFMON+CAP_DAC_READ_SEARCH; kernel < 5.5, "
                                "CONFIG_DEBUG_INFO_BTF=n, or verifier rejected)",
                 .attach_advice = "no programs attached (apply CAP_BPF+CAP_PERFMON+CAP_DAC_READ_SEARCH; kernel "
                                  "missing BTF for sched_switch tracepoint)"},
        .timeline_map = CRUCIBLE_PERF_FOREIGN_NAME("sched_timeline"),
        .counter_map = CRUCIBLE_PERF_FOREIGN_NAME("cs_count"),
        .counter_fallback = "context_switches() returns 0",
    };
    return detail::load_ring<SchedTpBtf>(ctx, spec);
}

uint64_t SchedTpBtf::context_switches() const noexcept { return detail::ring_count(state_); }

::fixy::View<const TimelineSchedEvent> SchedTpBtf::timeline_view() const noexcept {
    return detail::ring_events(state_);
}

uint64_t SchedTpBtf::timeline_write_index() const noexcept { return detail::ring_write_index(state_); }

::fixy::Refined<::fixy::bounded_above<8>, std::size_t> SchedTpBtf::attached_programs() const noexcept {
    return detail::attached_programs(state_);
}

::fixy::Refined<::fixy::bounded_above<8>, std::size_t> SchedTpBtf::attach_failures() const noexcept {
    return detail::attach_failures(state_);
}

SchedTpBtf::Snapshot SchedTpBtf::snapshot() const noexcept {
    return Snapshot{
        .ctx_switches = context_switches(),
        .timeline_index = timeline_write_index(),
    };
}

}  // namespace crucible::perf
