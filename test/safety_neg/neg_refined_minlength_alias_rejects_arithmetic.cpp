// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 4 for FIXY-U-160 MinLength<N, T> /
// MaxBounded<Max, T> §XVI parameterised-alias surface.
//
// Premise: `safety::MinLength<N, T>` is the named template alias for
// `Refined<length_ge<N>, T>` — closes the §XVI "every load-bearing
// predicate gets a named alias" discipline gap for the *parameterised*
// length-ge family.  The alias inherits Refined's `requires
// PredicateInvocableOn<length_ge<N>, T>` concept gate at construction.
//
// `length_ge<N>(c)` is defined as `c.size() >= N` (lambda body in
// safety/Refined.h:351).  A type without `.size()` — arithmetic types
// being the most common production-site misuse — fails the substitution
// because `c.size()` is not a valid expression on `int const&`.
// PredicateInvocableOn<length_ge<N>, int> is therefore false.
//
// Mismatch class for this fixture (distinct from companion fixtures):
//   * THIS fixture (Class C — arithmetic→container surface): the
//     parameterised predicate body's `.size()` member call fails
//     substitution on a scalar type.  A confused caller who reaches
//     for "minimum-length integer" (perhaps confusing it with
//     `bounded_below`) would naturally type `MinLength<5, int>`.  The
//     alias must reject at the concept boundary, not silently fall
//     through to a SFINAE wall inside the contract's `pre(length_ge<5>
//     (v))` clause.
//   * Companion #2 (Class L — N=0 vacuous-but-well-formed): proves
//     that N=0 is intentionally permitted (the predicate is vacuous
//     but well-formed); see U-160 doc-comment in Refined.h for the
//     soundness rationale of NOT gating N=0 at the alias.
//   * Companion #3 (Class M — bounded_above arithmetic non-comparable):
//     bounded_above<Max>(x) requires `x < Max` which fails for types
//     with no operator< overload.
//   * Companion #4 (Class B — bounded_above wrong-type-of-Max NTTP):
//     auto-NTTP infers Max's type; passing a Max value of the wrong
//     type relative to T causes a substitution-class mismatch.
//
// Substring "PredicateInvocableOn" pins the diagnostic — GCC 16 emits
// the concept name in the "constraint requires" line of the
// `MinLength<5, int>{...}` construction site.
//
// FIXY-U-160 HS14 fixture #1 of 4, paired with companion fixtures to
// witness the parameterised-alias surface's concept gate fires across
// four distinct mismatch classes (Class C / L / M / B).

#include <crucible/safety/Refined.h>

int main() {
    using crucible::safety::MinLength;

    // VIOLATION: MinLength<5, int> = Refined<length_ge<5>, int>; ctor's
    // `requires PredicateInvocableOn<length_ge<5>, int>` is false
    // because int has no .size() member — concept-violation diagnostic
    // at this line, NOT a SFINAE wall inside <contracts>.
    MinLength<5, int> bad{42};
    (void)bad;
    // ERROR: no matching constructor; constraints not satisfied —
    // 'PredicateInvocableOn' [with auto Pred = length_ge<5>, T = int]
    return 0;
}
