#include <crucible/perf/SchedTpBtf.h>

#include <crucible/perf/detail/BpfHub.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

extern "C" {
extern const unsigned char sched_tp_btf_bpf_bytecode[];
extern const unsigned int sched_tp_btf_bpf_bytecode_len;
}

namespace crucible::perf {

namespace {
struct SchedTpBtfRingTag {};
}  // namespace

struct SchedTpBtf::State : detail::RingState<SchedTpBtfRingTag, TimelineSchedEvent> {};

SchedTpBtf::SchedTpBtf() noexcept = default;
SchedTpBtf::SchedTpBtf(SchedTpBtf&&) noexcept = default;
SchedTpBtf& SchedTpBtf::operator=(SchedTpBtf&&) noexcept = default;
SchedTpBtf::~SchedTpBtf() = default;

std::optional<SchedTpBtf> SchedTpBtf::load(::fixy::InitLoadCtx const& ctx) noexcept {
    const detail::RingSpec spec{
        .load = {.facade = "sched_tp_btf",
                 .object_name = "crucible_sched_tp_btf",
                 .bytecode = std::span{sched_tp_btf_bpf_bytecode,
                                       static_cast<std::size_t>(sched_tp_btf_bpf_bytecode_len)},
                 .probe_tracepoints = false,
                 .load_advice = "(apply CAP_BPF+CAP_PERFMON+CAP_DAC_READ_SEARCH; kernel < 5.5, "
                                "CONFIG_DEBUG_INFO_BTF=n, or verifier rejected)",
                 .attach_advice = "no programs attached (apply CAP_BPF+CAP_PERFMON+CAP_DAC_READ_SEARCH; kernel "
                                  "missing BTF for sched_switch tracepoint)"},
        .timeline_map = "sched_timeline",
        .counter_map = "cs_count",
        .counter_fallback = "context_switches() returns 0",
    };
    auto state = detail::load_ring<State>(ctx, spec);
    if (state == nullptr) return std::nullopt;
    SchedTpBtf hub;
    hub.state_ = std::move(state);
    return hub;
}

uint64_t SchedTpBtf::context_switches() const noexcept { return state_ != nullptr ? state_->count() : 0; }

::fixy::Borrowed<const TimelineSchedEvent, SchedTpBtf> SchedTpBtf::timeline_view() const noexcept {
    return state_ != nullptr ? state_->events<SchedTpBtf>() : ::fixy::Borrowed<const TimelineSchedEvent, SchedTpBtf>{};
}

uint64_t SchedTpBtf::timeline_write_index() const noexcept { return state_ != nullptr ? state_->write_index() : 0; }

::fixy::Refined<::fixy::bounded_above<8>, std::size_t> SchedTpBtf::attached_programs() const noexcept {
    return detail::attached_programs(state_.get());
}

::fixy::Refined<::fixy::bounded_above<8>, std::size_t> SchedTpBtf::attach_failures() const noexcept {
    return detail::attach_failures(state_.get());
}

SchedTpBtf::Snapshot SchedTpBtf::snapshot() const noexcept {
    return Snapshot{
        .ctx_switches = context_switches(),
        .timeline_index = timeline_write_index(),
    };
}

}  // namespace crucible::perf
