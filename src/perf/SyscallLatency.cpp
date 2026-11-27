#include <crucible/perf/SyscallLatency.h>

#include <crucible/perf/detail/BpfHub.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace crucible::perf {

// The tag of the mapping of the facade.  The mapping takes the singleton
// root of the tag, so the tag has a stable identity and external linkage.
struct SyscallLatencyRingTag {};

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
                 .bytecode = detail::syscall_latency_bpf_object(),
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

::fixy::mmap::SharedView<const TimelineSyscallEvent>
SyscallLatency::timeline_view(this SyscallLatency const& self) noexcept {
    return detail::ring_events(OwnerKey{}, self.state_);
}

uint64_t SyscallLatency::timeline_write_index() const noexcept { return detail::ring_write_index(state_); }

uint64_t SyscallLatency::timeline_dropped() const noexcept { return detail::ring_dropped(state_); }

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
