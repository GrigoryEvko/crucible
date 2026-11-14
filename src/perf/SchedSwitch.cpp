#include <crucible/perf/SchedSwitch.h>

#include <crucible/perf/detail/BpfHub.h>

#include <cstddef>
#include <cstdint>
#include <utility>

extern "C" {
extern const unsigned char sched_switch_bpf_bytecode[];
extern const unsigned int sched_switch_bpf_bytecode_len;
}

namespace crucible::perf {

namespace {
struct SchedSwitchRingTag {};
}  // namespace

struct SchedSwitch::State : detail::RingState<SchedSwitchRingTag, TimelineSchedEvent> {};

SchedSwitch::SchedSwitch(::fixy::Box<State> state) noexcept
    : state_{::fixy::Option<::fixy::Box<State>>::some(std::move(state))} {}
SchedSwitch::SchedSwitch(SchedSwitch&&) noexcept = default;
SchedSwitch& SchedSwitch::operator=(SchedSwitch&&) noexcept = default;
SchedSwitch::~SchedSwitch() = default;

::fixy::Option<SchedSwitch> load_sched_switch(::fixy::InitLoadCtx const& ctx) noexcept {
    const detail::RingSpec spec{
        .load = {.facade = CRUCIBLE_PERF_FOREIGN_NAME("sched_switch"),
                 .object_name = CRUCIBLE_PERF_FOREIGN_NAME("crucible_sched_switch"),
                 .bytecode = {.abi = sched_switch_bpf_bytecode,
                              .size = static_cast<std::size_t>(sched_switch_bpf_bytecode_len)},
                 .attach_advice = "no programs attached (apply CAP_BPF+CAP_PERFMON+CAP_DAC_READ_SEARCH; kernel "
                                  "missing sched_switch tracepoint)"},
        .timeline_map = CRUCIBLE_PERF_FOREIGN_NAME("sched_timeline"),
        .counter_map = CRUCIBLE_PERF_FOREIGN_NAME("cs_count"),
        .counter_fallback = "context_switches() returns 0",
    };
    return detail::load_ring<SchedSwitch>(ctx, spec);
}

uint64_t SchedSwitch::context_switches() const noexcept { return detail::ring_count(state_); }

::fixy::Borrowed<const TimelineSchedEvent, SchedSwitch> SchedSwitch::timeline_view() const noexcept {
    return detail::ring_events<SchedSwitch>(state_);
}

uint64_t SchedSwitch::timeline_write_index() const noexcept { return detail::ring_write_index(state_); }

::fixy::MaxBounded<8, std::size_t> SchedSwitch::attached_programs() const noexcept {
    return detail::attached_programs(state_);
}

::fixy::MaxBounded<8, std::size_t> SchedSwitch::attach_failures() const noexcept {
    return detail::attach_failures(state_);
}

SchedSwitch::Snapshot SchedSwitch::snapshot() const noexcept {
    return Snapshot{
        .ctx_switches = context_switches(),
        .timeline_index = timeline_write_index(),
    };
}

}  // namespace crucible::perf
