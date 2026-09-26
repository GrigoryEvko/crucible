#include <crucible/perf/SyscallLatency.h>

#include <crucible/perf/detail/BpfHub.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

extern "C" {
extern const unsigned char syscall_latency_bpf_bytecode[];
extern const unsigned int syscall_latency_bpf_bytecode_len;
}

namespace crucible::perf {

namespace {
struct SyscallLatencyRingTag {};
}  // namespace

struct SyscallLatency::State : detail::RingState<SyscallLatencyRingTag, TimelineSyscallEvent> {};

SyscallLatency::SyscallLatency() noexcept = default;
SyscallLatency::SyscallLatency(SyscallLatency&&) noexcept = default;
SyscallLatency& SyscallLatency::operator=(SyscallLatency&&) noexcept = default;
SyscallLatency::~SyscallLatency() = default;

std::optional<SyscallLatency> SyscallLatency::load(::fixy::InitLoadCtx const&) noexcept {
    // The attach is all-or-nothing.  With the enter tracepoint attached but
    // the exit one missing, every recorded start entry stays unconsumed, and
    // the facade records useless half-events until the LRU hash map evicts
    // them.
    const detail::RingSpec spec{
        .load = {.facade = "syscall_latency",
                 .object_name = "crucible_syscall_latency",
                 .bytecode = std::span{syscall_latency_bpf_bytecode,
                                       static_cast<std::size_t>(syscall_latency_bpf_bytecode_len)},
                 .min_attached = 2,
                 .attach_advice = "expected 2 tracepoint attachments (raw_syscalls/sys_enter and sys_exit), got "
                                  "fewer (kernel missing raw_syscalls tracepoints, or partial CAP_BPF rejection)"},
        .timeline_map = "syscall_timeline",
        .counter_map = "total_syscalls",
        .counter_fallback = "total_syscalls() returns 0",
    };
    auto state = detail::load_ring<State>(spec);
    if (state == nullptr) return std::nullopt;
    SyscallLatency hub;
    hub.state_ = std::move(state);
    return hub;
}

uint64_t SyscallLatency::total_syscalls() const noexcept { return state_ != nullptr ? state_->count() : 0; }

::fixy::Borrowed<const TimelineSyscallEvent, SyscallLatency> SyscallLatency::timeline_view() const noexcept {
    return state_ != nullptr ? state_->events<SyscallLatency>()
                             : ::fixy::Borrowed<const TimelineSyscallEvent, SyscallLatency>{};
}

uint64_t SyscallLatency::timeline_write_index() const noexcept {
    return state_ != nullptr ? state_->write_index() : 0;
}

::fixy::Refined<::fixy::bounded_above<8>, std::size_t> SyscallLatency::attached_programs() const noexcept {
    return detail::attached_programs(state_.get());
}

::fixy::Refined<::fixy::bounded_above<8>, std::size_t> SyscallLatency::attach_failures() const noexcept {
    return detail::attach_failures(state_.get());
}

SyscallLatency::Snapshot SyscallLatency::snapshot() const noexcept {
    return Snapshot{
        .total_syscalls = total_syscalls(),
        .timeline_index = timeline_write_index(),
    };
}

}  // namespace crucible::perf
