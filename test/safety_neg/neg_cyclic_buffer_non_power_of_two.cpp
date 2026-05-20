// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 2 for safety::CyclicBuffer<T, N> (#1084 - CyclicBuffer
// piece).
//
// Premise: CyclicBuffer<T, N> requires N to be a power of two — the
// CLASS-TEMPLATE requires clause `(N > 0 && (N & (N - 1)) == 0)` is what
// lets its composed Cyclic<size_t, N> write cursor mask `counter & (N-1)`
// equal `counter % N`.  A non-power-of-two N (here 6) MUST be a compile
// error: the ring would silently alias slots 5,6,7 onto 0..4 once the
// cursor passes 4, corrupting every recent(i) reverse-scan.
//
// This is the CLASS-LEVEL rejection (distinct from fixture #2's
// MEMBER-FUNCTION rejection): the constraint fires before any member is
// even named, so `CyclicBuffer<int, 6>` has no valid specialization.
//
// Expected diagnostic: "constraints not satisfied" / "associated
// constraints are not satisfied" / "no matching template" pointing at the
// CyclicBuffer<int, 6> instantiation.

#include <crucible/safety/CyclicBuffer.h>

namespace saf = crucible::safety;

int main() {
    // Bridge fires: 6 is not a power of two → (6 & 5) != 0 → the
    // class-template requires-clause rejects, no fallback exists.
    saf::CyclicBuffer<int, 6> bad{};
    (void)bad;
    return 0;
}
