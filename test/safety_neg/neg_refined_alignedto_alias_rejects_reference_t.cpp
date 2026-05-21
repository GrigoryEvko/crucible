// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 4 for FIXY-U-161 AlignedTo<N, T> / WithinRange
// <L, H, T> §XVI parameterised-alias closure.
//
// Premise: `AlignedTo<N, T>` requires T to be a pointer type (per the
// `auto* p` parameter of `aligned<N>`).  This fixture witnesses the
// rejection of REFERENCE T, which is a structurally distinct
// mismatch class from companion #1 (scalar T):
//   * Scalar `int`     — fundamental value type, no pointer-ness
//   * Reference `int&` — has reference-binding semantics, looks
//                        pointer-like at the source level BUT C++
//                        references are NOT pointers; auto* refuses
//                        to bind to a reference.
//
// The two mismatch classes co-witness the discipline: the concept
// gate is structural — it rejects "non-pointer T" at the LEVEL of
// pointer-ness, not at the level of "T cannot be addressed".  A
// reference IS addressable (taking the address of the referent works
// at runtime) but the CONCEPT level requires the TYPE to satisfy the
// pointer-substitution rule of auto*, which references do not.
//
// Production-site misuse pattern: a developer writes `int& r =
// some_value; AlignedTo<64, decltype(r)> a{r};` thinking "decltype(r)
// is int&, the reference type, and references-to-aligned-storage are
// a thing".  Wrong — the alias must reject at the construction site
// to surface the conceptual mistake at compile-time, not after a
// SFINAE wall in <contracts>.
//
// Note: by-value `int` and by-reference `int&` represent the TWO
// canonical non-pointer-T misuse patterns; auto-NTTP and concept-
// gate path treat them differently in error message but identically
// in rejection — both correctly fail.
//
// Substring "PredicateInvocableOn" pins the diagnostic.

#include <crucible/safety/Refined.h>

int main() {
    using crucible::safety::AlignedTo;

    int storage = 42;
    int& ref = storage;

    // VIOLATION: AlignedTo<64, int&> = Refined<aligned<64>, int&>.
    // aligned<N>'s `auto* p` cannot deduce a pointer from a reference
    // type — PredicateInvocableOn<aligned<64>, int&> is false.
    AlignedTo<64, int&> bad{ref};
    (void)bad;
    // ERROR: no matching constructor; constraints not satisfied —
    // 'PredicateInvocableOn' [with auto Pred = aligned<64>, T = int&]
    return 0;
}
