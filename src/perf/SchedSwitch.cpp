#include <crucible/perf/SchedSwitch.h>

#include <crucible/perf/detail/BpfHub.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

extern "C" {
extern const unsigned char sched_switch_bpf_bytecode[];
extern const unsigned int sched_switch_bpf_bytecode_len;
}

namespace crucible::perf {

namespace {
struct SchedSwitchRingTag {};
}  // namespace

struct SchedSwitch::State : detail::RingState<SchedSwitchRingTag, TimelineSchedEvent> {};

SchedSwitch::SchedSwitch() noexcept = default;
SchedSwitch::SchedSwitch(SchedSwitch&&) noexcept = default;
SchedSwitch& SchedSwitch::operator=(SchedSwitch&&) noexcept = default;
SchedSwitch::~SchedSwitch() = default;

std::optional<SchedSwitch> SchedSwitch::load(::fixy::InitLoadCtx const&) noexcept {
    const detail::RingSpec spec{
        .load = {.facade = "sched_switch",
                 .object_name = "crucible_sched_switch",
                 .bytecode = std::span{sched_switch_bpf_bytecode,
                                       static_cast<std::size_t>(sched_switch_bpf_bytecode_len)},
                 .attach_advice = "no programs attached (apply CAP_BPF+CAP_PERFMON+CAP_DAC_READ_SEARCH; kernel "
                                  "missing sched_switch tracepoint)"},
        .timeline_map = "sched_timeline",
        .counter_map = "cs_count",
        .counter_fallback = "context_switches() returns 0",
    };
    auto state = detail::load_ring<State>(spec);
    if (state == nullptr) return std::nullopt;
    SchedSwitch hub;
    hub.state_ = std::move(state);
    return hub;
}

uint64_t SchedSwitch::context_switches() const noexcept { return state_ != nullptr ? state_->count() : 0; }

::fixy::Borrowed<const TimelineSchedEvent, SchedSwitch> SchedSwitch::timeline_view() const noexcept {
    return state_ != nullptr ? state_->events<SchedSwitch>() : ::fixy::Borrowed<const TimelineSchedEvent, SchedSwitch>{};
}

uint64_t SchedSwitch::timeline_write_index() const noexcept { return state_ != nullptr ? state_->write_index() : 0; }

::fixy::MaxBounded<8, std::size_t> SchedSwitch::attached_programs() const noexcept {
    return detail::attached_programs(state_.get());
}

::fixy::MaxBounded<8, std::size_t> SchedSwitch::attach_failures() const noexcept {
    return detail::attach_failures(state_.get());
}

SchedSwitch::Snapshot SchedSwitch::snapshot() const noexcept {
    return Snapshot{
        .ctx_switches = context_switches(),
        .timeline_index = timeline_write_index(),
    };
}

}  // namespace crucible::perf
