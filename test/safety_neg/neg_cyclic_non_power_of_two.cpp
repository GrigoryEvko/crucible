// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 2 for safety::Cyclic<T, N> (#1084 - Cyclic piece).
//
// Premise: Cyclic<T, N> requires N to be a power of two — the requires
// clause `(N & (N - 1)) == 0` is what makes `counter & (N-1)` equal
// `counter % N`.  A non-power-of-two N (here 6) MUST be a compile
// error, because `& (N-1)` would silently compute the WRONG slot
// (6 & 5 = 4, not 6 % 5; the mask 5 only spans 3 bits, so slots 5,6,7
// alias into 0..5 incorrectly).
//
// Without this rejection, `Cyclic<uint32_t, 6>` would instantiate and
// silently corrupt every ring-slot computation — the kind of bug that
// surfaces as rare data races on the 6th, 7th, 8th... insertion.  The
// power-of-two requires-clause closes that at the type-system boundary.
//
// Expected diagnostic: "constraints not satisfied" / "associated
// constraints are not satisfied" / "no matching template" pointing at
// the Cyclic<uint32_t, 6> instantiation.

#include <crucible/safety/Cyclic.h>

namespace saf = crucible::safety;

int main() {
    // Bridge fires: 6 is not a power of two → (6 & 5) != 0 → the
    // requires-clause rejects, no fallback specialization exists.
    saf::Cyclic<unsigned int, 6> bad{};
    (void)bad;
    return 0;
}
