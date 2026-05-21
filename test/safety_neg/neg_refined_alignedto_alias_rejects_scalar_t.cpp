// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 4 for FIXY-U-161 AlignedTo<N, T> / WithinRange
// <L, H, T> §XVI parameterised-alias closure.
//
// Premise: `safety::AlignedTo<N, T>` is the named template alias for
// `Refined<aligned<N>, T>`.  The underlying predicate `aligned<N>`
// takes `auto* p` — its operator() body bit-casts the pointer to
// uintptr_t and tests `(addr & (N-1)) == 0`.  Therefore T MUST be a
// pointer type; scalar value types (int, std::uint64_t, etc.) are
// rejected at the PredicateInvocableOn concept gate because the
// pointer-parameter substitution fails.
//
// Mismatch class for this fixture (distinct from companion fixtures):
//   * THIS fixture (Class P — pointer-required, scalar offered):
//     a confused caller passes a scalar T (`int`) to AlignedTo<64, int>
//     thinking "AlignedTo means aligned-on-64-bytes integer" — but
//     aligned<N> is pointer-alignment, not value-alignment.  The
//     concept gate rejects with PredicateInvocableOn-false.
//   * Companion #2 (Class R — reference T): AlignedTo<64, int&>.
//     Reference T also fails the auto* pointer-parameter deduction
//     but for a DIFFERENT structural reason (references are not
//     pointers even though they share spelling vibes).
//   * Companion #3 (Class C — comparison missing on struct):
//     WithinRange<0, 100, NoOrderStruct>.
//   * Companion #4 (Class H — half-ordering interface):
//     WithinRange<0, 100, OnlyLeqStruct> — struct has operator<= but
//     no operator>=.  The in_range body is `x >= L && x <= H`; the
//     dual-operator requirement is the distinguishing feature of
//     in_range vs bounded_above (single operator<).
//
// Substring "PredicateInvocableOn" pins the diagnostic — GCC 16 emits
// the concept name in the "constraint requires" line of the
// `AlignedTo<64, int>{...}` construction site.

#include <crucible/safety/Refined.h>

int main() {
    using crucible::safety::AlignedTo;

    // VIOLATION: AlignedTo<64, int> = Refined<aligned<64>, int>; ctor's
    // `requires PredicateInvocableOn<aligned<64>, int>` is false because
    // aligned<N>'s `auto* p` parameter cannot bind to a scalar `int`.
    // Concept-violation diagnostic at this line, NOT a SFINAE wall
    // inside <contracts>.
    AlignedTo<64, int> bad{42};
    (void)bad;
    // ERROR: no matching constructor; constraints not satisfied —
    // 'PredicateInvocableOn' [with auto Pred = aligned<64>, T = int]
    return 0;
}
