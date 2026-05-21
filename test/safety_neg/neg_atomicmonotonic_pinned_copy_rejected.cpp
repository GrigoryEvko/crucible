// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: copying an instance of `crucible::safety::AtomicMonotonic
// <T, Cmp>`.  The wrapper privately inherits `Pinned<AtomicMonotonic
// <T, Cmp>>` (Mutation.h:885) which DELETES copy/move with the
// "stable address — address-as-identity or interior pointers into
// own storage" reason.  AtomicMonotonic's address IS the identity of
// the cross-thread atomic — moving the wrapper would leave one
// thread's atomic_ref pointing at the old slot while the other
// continues writing the new slot.  Pinned makes that bug a compile
// error.
//
// Discipline rationale (Mutation.h:880-886):
//   `AtomicMonotonic<T, Cmp>` is the typed wrapper for SPSC head /
//   tail counters, MPSC ticket counters, Vyukov MPMC per-cell
//   sequences.  The `std::atomic<T> value_` field is at a fixed
//   address that producer and consumer threads target — moving the
//   wrapper would break the cross-thread happens-before that the
//   acquire/release pair establishes against THAT specific address.
//
//   This is structurally distinct from the typed-overload axis
//   (covered by the sibling Class U-non-trivially-copyable fixture):
//   - This file: ADDRESS STABILITY — copying creates two addresses
//     claiming to be the same atomic, breaking SPSC physics.
//   - Sibling:   PAYLOAD TRIVIALITY — instantiating with non-trivially-
//     copyable T fails the requires clause, distinct from copying
//     the wrapper itself.
//
// HS14 — distinct-class fixture pair for AtomicMonotonic:
//   * Class T-pinned-copy (THIS file): Pinned-derived copy deletion
//     — pins the address-stability discipline.
//   * Class U-non-trivially-copyable (sibling): requires
//     `std::is_trivially_copyable_v<T>` rejects non-trivial T at
//     template instantiation — pins the atomic-payload triviality
//     discipline.
//
// FIXY-U-147 — first neg-compile pair for safety::AtomicMonotonic
// (closes the AtomicMonotonic slice of backlog #146 A8-P2 alongside
// U-140..U-146).

#include <crucible/safety/Mutation.h>

namespace {
    using CounterT = ::crucible::safety::AtomicMonotonic<std::uint64_t>;
}

// Anchor: in-place construction of an AtomicMonotonic is allowed —
// the only path to OBTAIN one is direct ctor (Pinned protects copy /
// move, not construction).  This call compiles.
[[maybe_unused]] static CounterT anchor_make_atomic_monotonic() {
    return CounterT{0};
}

// VIOLATION: AtomicMonotonic inherits a deleted copy ctor from
// Pinned<AtomicMonotonic<uint64_t>>.  Attempting to copy-construct
// triggers the deletion with the load-bearing reason.  GCC emits
// "use of deleted function" with the Pinned "stable address" reason
// string in the diagnostic chain.
[[maybe_unused]] static CounterT offending_atomic_monotonic_copy(
    const CounterT& source)
{
    return CounterT{source};   // ERROR: copy ctor deleted by Pinned
}

int main() { return 0; }
