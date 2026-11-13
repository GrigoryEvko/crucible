#include <crucible/perf/SyscallLatency.h>

#include <crucible/perf/detail/BpfHub.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

extern "C" {
extern const unsigned char syscall_latency_bpf_bytecode[];
extern const unsigned int syscall_latency_bpf_bytecode_len;
}

namespace crucible::perf {

namespace {
struct SyscallLatencyRingTag {};
}  // namespace

struct SyscallLatency::State : detail::RingState<SyscallLatencyRingTag, TimelineSyscallEvent> {};

SyscallLatency::SyscallLatency(::fixy::Box<State> state) noexcept
    : state_{::fixy::Option<::fixy::Box<State>>::some(std::move(state))} {}
SyscallLatency::SyscallLatency(SyscallLatency&&) noexcept = default;
SyscallLatency& SyscallLatency::operator=(SyscallLatency&&) noexcept = default;
SyscallLatency::~SyscallLatency() = default;

::fixy::Option<SyscallLatency> load_syscall_latency(::fixy::InitLoadCtx const& ctx) noexcept {
    // The attach is all-or-nothing.  With the enter tracepoint attached but
    // the exit one missing, every recorded start entry stays unconsumed, and
    // the facade records useless half-events until the LRU hash map evicts
    // them.
    const detail::RingSpec spec{
        .load = {.facade = CRUCIBLE_PERF_FOREIGN_NAME("syscall_latency"),
                 .object_name = CRUCIBLE_PERF_FOREIGN_NAME("crucible_syscall_latency"),
                 .bytecode = std::span{syscall_latency_bpf_bytecode,
                                       static_cast<std::size_t>(syscall_latency_bpf_bytecode_len)},
                 .min_attached = 2,
                 .attach_advice = "expected 2 tracepoint attachments (raw_syscalls/sys_enter and sys_exit), got "
                                  "fewer (kernel missing raw_syscalls tracepoints, or partial CAP_BPF rejection)"},
        .timeline_map = CRUCIBLE_PERF_FOREIGN_NAME("syscall_timeline"),
        .counter_map = CRUCIBLE_PERF_FOREIGN_NAME("total_syscalls"),
        .counter_fallback = "total_syscalls() returns 0",
    };
    return detail::load_ring<SyscallLatency>(ctx, spec);
}

uint64_t SyscallLatency::total_syscalls() const noexcept { return detail::ring_count(state_); }

::fixy::Borrowed<const TimelineSyscallEvent, SyscallLatency> SyscallLatency::timeline_view() const noexcept {
    return detail::ring_events<SyscallLatency>(state_);
}

uint64_t SyscallLatency::timeline_write_index() const noexcept { return detail::ring_write_index(state_); }

::fixy::Refined<::fixy::bounded_above<8>, std::size_t> SyscallLatency::attached_programs() const noexcept {
    return detail::attached_programs(state_);
}

::fixy::Refined<::fixy::bounded_above<8>, std::size_t> SyscallLatency::attach_failures() const noexcept {
    return detail::attach_failures(state_);
}

SyscallLatency::Snapshot SyscallLatency::snapshot() const noexcept {
    return Snapshot{
        .total_syscalls = total_syscalls(),
        .timeline_index = timeline_write_index(),
    };
}

}  // namespace crucible::perf
