#pragma once

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/Refined.h>
#include <fixy/os/OwnedMmap.h>
#include <foundation/core/Array.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Row.h>

#include <cstddef>
#include <cstdint>

namespace crucible::perf {

namespace detail {
struct FacadeDoor;
}  // namespace detail

// The kernel-side program writes into the same byte offsets userspace
// reads.  commit is the commit word of a slot, and it holds the ticket of
// the write (the ring ticket of fixy/os/OwnedMmap.h).  The other fields of
// a slot are a record only for a reader that asks for that ticket.
//
// The struct is 32 bytes, which divides the 64-byte cache line evenly, so
// no slot in the events array straddles two lines.
struct TimelineSchedEvent {
    uint64_t off_cpu_ns;
    uint32_t tid;
    uint32_t on_cpu;
    uint64_t ts_ns;  // kernel monotonic clock
    uint64_t commit;  // the ticket of the write
};

// write_idx counts each claim of a slot, and dropped counts the claims whose
// write dropped (the ring ticket of fixy/os/OwnedMmap.h).
struct TimelineHeader {
    uint64_t write_idx;
    uint64_t dropped;
    ::fixy::FixedArray<uint64_t, 6> _pad;
};

// This value mirrors the ring size the kernel-side program was built
// with.  Changing one side alone breaks the shared layout.
constexpr uint32_t TIMELINE_CAPACITY = 4096;
[[maybe_unused]] constexpr uint32_t TIMELINE_MASK = TIMELINE_CAPACITY - 1;

class SchedSwitch {
public:
    // Both fields count upward forever, so an ordered post-minus-pre
    // subtraction never underflows.  The saturation below guards
    // against a caller that swapped the two snapshots.
    struct Snapshot {
        uint64_t ctx_switches = 0;
        uint64_t timeline_index = 0;

        [[nodiscard]] Snapshot operator-(const Snapshot& older) const noexcept {
            return Snapshot{
                .ctx_switches = ::fixy::saturating_sub(ctx_switches, older.ctx_switches),
                .timeline_index = ::fixy::saturating_sub(timeline_index, older.timeline_index),
            };
        }
    };

    [[nodiscard]] Snapshot snapshot() const noexcept;

    // Counts only this process, from the load onward.  Reading it
    // costs a map-lookup syscall: the counter lives in a one-element
    // array map, and the kernel-side program does not declare that
    // map mmap-able.
    [[nodiscard]] uint64_t context_switches() const noexcept;

    // Only the loading thread is registered with the kernel-side
    // program, so off-CPU events on the other threads of this process
    // never reach the ring.
    //
    // The kernel writes this memory while the reader walks it, so read
    // the record of a ticket through its commit word, with
    // read_ticket<&TimelineSchedEvent::commit>.  A record that the ring
    // wrote over, that is not whole yet, or whose write dropped gives no
    // value.
    [[nodiscard]] ::fixy::mmap::SharedView<const TimelineSchedEvent> timeline_view(this SchedSwitch const& self
                                                                                   CRUCIBLE_LIFETIMEBOUND) noexcept;
    void timeline_view(this SchedSwitch const&& self) =
        delete("the view borrows the ring of the facade, which a temporary facade ends");

    // The index counts claims forever, so the newest ticket is
    // `write_idx - 1`, and its slot is that ticket masked to the capacity.
    [[nodiscard]] uint64_t timeline_write_index() const noexcept;

    // The count of the claims whose write dropped.  A count that grows
    // with each lap of the ring shows a slot that a program never left.
    [[nodiscard]] uint64_t timeline_dropped() const noexcept;

    [[nodiscard]] ::fixy::MaxBounded<8, std::size_t> attached_programs() const noexcept;

    // Set CRUCIBLE_PERF_VERBOSE=1 in the environment to see why an
    // attach failed.
    [[nodiscard]] ::fixy::MaxBounded<8, std::size_t> attach_failures() const noexcept;

    SchedSwitch(const SchedSwitch&) = delete("SchedSwitch owns unique BPF object + mmap — copying would double-close");
    SchedSwitch&
    operator=(const SchedSwitch&) = delete("SchedSwitch owns unique BPF object + mmap — copying would double-close");
    SchedSwitch(SchedSwitch&&) noexcept;
    SchedSwitch& operator=(SchedSwitch&&) noexcept;
    ~SchedSwitch();

private:
    // The door builds the facade in each load.
    friend struct detail::FacadeDoor;
    // The key of the owner door of the Box of the state (borrow_owned of
    // foundation/core/Ref.h).
    using OwnerKey = ::foundation::core::box_owner_key<SchedSwitch>;
    struct State;
    explicit SchedSwitch(::fixy::Box<State> state) noexcept;

    // A moved-from facade holds no state.  Each constructor sets the member,
    // so it has no default initializer: one here would end a Box in each unit
    // that sees only the declaration of State.
    ::fixy::Option<::fixy::Box<State>> state_;
};

// Returns none when the kernel refuses the load: no CAP_BPF or
// CAP_PERFMON, a kernel without the tracepoint, or a verifier rejection.
// A diagnostic line goes to stderr unless CRUCIBLE_PERF_QUIET=1 is set in
// the environment, and CRUCIBLE_PERF_VERBOSE=1 forwards the libbpf INFO
// and WARN messages as well.
//
// The load takes the startup load context.  Its row carries Block,
// because the load waits in the kernel while the verifier examines the
// program.  The load stands outside the class, because an Option of a
// facade needs the complete facade.
[[nodiscard]] ::fixy::Option<SchedSwitch> load_sched_switch(::fixy::InitLoadCtx const&) noexcept;

// The load attaches to the sched_switch tracepoint, maps the timeline
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

using sched_switch_required_row =
    ::foundation::effects::Row<::foundation::effects::Effect::Alloc, ::foundation::effects::Effect::IO,
                               ::foundation::effects::Effect::Block>;

template <class Ctx>
concept CtxFitsSchedSwitchMint = ::foundation::effects::IsExecCtx<Ctx>
                              && ::foundation::effects::Subrow<sched_switch_required_row, typename Ctx::row_type>;

template <::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsSchedSwitchMint<Ctx>
[[nodiscard]] inline ::fixy::Option<SchedSwitch> mint_sched_switch(Ctx const&,
                                                                   ::fixy::InitLoadCtx const& init) noexcept {
    return load_sched_switch(init);
}

}  // namespace crucible::perf
