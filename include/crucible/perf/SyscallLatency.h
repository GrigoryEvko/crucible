#pragma once

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/Refined.h>
#include <fixy/os/OwnedMmap.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Row.h>

#include <cstddef>
#include <cstdint>

namespace crucible::perf {

// The kernel-side program writes into the same byte offsets userspace
// reads.  commit is the commit word of a slot, and it holds the ticket of
// the write (the ring ticket of fixy/os/OwnedMmap.h).  The other fields of
// a slot are a record only for a reader that asks for that ticket.
//
// The struct is 32 bytes, which divides the 64-byte cache line evenly, so
// no slot in the events array straddles two lines.
struct TimelineSyscallEvent {
    uint64_t duration_ns;
    uint32_t tid;
    uint32_t syscall_nr;
    uint64_t ts_ns;  // kernel monotonic clock
    uint64_t commit;  // the ticket of the write
};

}  // namespace crucible::perf

#include <crucible/perf/SchedSwitch.h>  // for TimelineHeader, TIMELINE_CAPACITY

namespace crucible::perf {

class SyscallLatency {
public:
    // Both fields count upward forever, so an ordered post-minus-pre
    // subtraction never underflows.  The saturation below guards
    // against a caller that swapped the two snapshots.  A
    // timeline_index delta larger than the ring capacity means the
    // window overwrote its own oldest events.
    struct Snapshot {
        uint64_t total_syscalls = 0;
        uint64_t timeline_index = 0;

        [[nodiscard]] Snapshot operator-(const Snapshot& older) const noexcept {
            return Snapshot{
                .total_syscalls = ::fixy::saturating_sub(total_syscalls, older.total_syscalls),
                .timeline_index = ::fixy::saturating_sub(timeline_index, older.timeline_index),
            };
        }
    };

    [[nodiscard]] Snapshot snapshot() const noexcept;

    // Reading the count costs a map-lookup syscall, and that syscall
    // is itself counted.  A tight loop over this accessor therefore
    // measures the loop rather than the workload.  Take one count
    // before the region and one after it.
    [[nodiscard]] uint64_t total_syscalls() const noexcept;

    // The kernel writes this memory while the reader walks it, so read
    // the record of a ticket through its commit word, with
    // read_ticket<&TimelineSyscallEvent::commit>.  A record that the ring
    // wrote over, that is not whole yet, or whose write dropped gives no
    // value.
    [[nodiscard]] ::fixy::mmap::SharedView<const TimelineSyscallEvent> timeline_view(this SyscallLatency const& self
                                                                                     CRUCIBLE_LIFETIMEBOUND) noexcept;
    void timeline_view(this SyscallLatency const&& self) =
        delete("the view borrows the ring of the facade, which a temporary facade ends");

    // The index counts claims forever, so the newest ticket is
    // `write_idx - 1`, and its slot is that ticket masked to the capacity.
    [[nodiscard]] uint64_t timeline_write_index() const noexcept;

    // The count of the claims whose write dropped.  A count that grows
    // with each lap of the ring shows a slot that a program never left.
    [[nodiscard]] uint64_t timeline_dropped() const noexcept;

    [[nodiscard]] ::fixy::Refined<::fixy::bounded_above<8>, std::size_t> attached_programs() const noexcept;

    // Set CRUCIBLE_PERF_VERBOSE=1 in the environment to see which
    // tracepoint was unavailable.
    [[nodiscard]] ::fixy::Refined<::fixy::bounded_above<8>, std::size_t> attach_failures() const noexcept;

    SyscallLatency(const SyscallLatency&) =
        delete("SyscallLatency owns unique BPF object + mmap — copying would double-close");
    SyscallLatency& operator=(const SyscallLatency&) =
        delete("SyscallLatency owns unique BPF object + mmap — copying would double-close");
    SyscallLatency(SyscallLatency&&) noexcept;
    SyscallLatency& operator=(SyscallLatency&&) noexcept;
    ~SyscallLatency();

private:
    // The door builds the facade in each load.
    friend struct detail::FacadeDoor;
    // The key of the owner door of the Box of the state (borrow_owned of
    // foundation/core/Ref.h).
    using OwnerKey = ::foundation::core::box_owner_key<SyscallLatency>;
    struct State;
    explicit SyscallLatency(::fixy::Box<State> state) noexcept;

    // A moved-from facade holds no state.  Each constructor sets the member,
    // so it has no default initializer: one here would end a Box in each unit
    // that sees only the declaration of State.
    ::fixy::Option<::fixy::Box<State>> state_;
};

// Both the syscall-enter and the syscall-exit tracepoint must attach.  A
// half attach records entry timestamps that no exit ever consumes, so the
// load fails instead.
//
// Returns none when the kernel refuses: no CAP_BPF or CAP_PERFMON, a
// kernel without the raw syscall tracepoints, or a verifier rejection.  A
// diagnostic line goes to stderr unless CRUCIBLE_PERF_QUIET=1 is set in
// the environment, and CRUCIBLE_PERF_VERBOSE=1 forwards the libbpf INFO
// and WARN messages as well.
//
// The load takes the startup load context.  Its row carries Block,
// because the load waits in the kernel while the verifier examines the
// program.
[[nodiscard]] ::fixy::Option<SyscallLatency> load_syscall_latency(::fixy::InitLoadCtx const&) noexcept;

// The load attaches to the raw syscall tracepoints, maps the timeline
// ring, and calls bpf(BPF_PROG_LOAD).  That last call enters the kernel
// and waits while the verifier walks the program.  The wait is why the
// row carries Block.  IO covers the bpf, perf_event_open and mmap
// traffic.  Alloc covers the state the load path takes from the heap.
//
// The startup load context and the background load context each claim
// Block on top of Alloc and IO, so each passes this gate.  The cold init
// context and the compile context stop at IO, and the gate refuses
// both.  The second argument is the startup load context that the load
// takes.

using syscall_latency_required_row =
    ::foundation::effects::Row<::foundation::effects::Effect::Alloc, ::foundation::effects::Effect::IO,
                               ::foundation::effects::Effect::Block>;

template <class Ctx>
concept CtxFitsSyscallLatencyMint = ::foundation::effects::IsExecCtx<Ctx>
                                 && ::foundation::effects::Subrow<syscall_latency_required_row, typename Ctx::row_type>;

template <::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsSyscallLatencyMint<Ctx>
[[nodiscard]] inline ::fixy::Option<SyscallLatency> mint_syscall_latency(Ctx const&,
                                                                         ::fixy::InitLoadCtx const& init) noexcept {
    return load_syscall_latency(init);
}

}  // namespace crucible::perf
