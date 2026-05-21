// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 4 for FIXY-U-160 MinLength<N, T> /
// MaxBounded<Max, T> §XVI parameterised-alias surface.
//
// Premise: `MinLength<N, T>` is `Refined<length_ge<N>, T>`.  A
// confused caller who wants to express both "length ≥ N" AND
// "positive elements" might naively try to nest two refinement
// aliases: `MinLength<5, Refined<positive, std::vector<int>>>`.
// This is wrong — `Refined<positive, std::vector<int>>` is a wrapper
// type with NO `.size()` method (the value is accessed via `.value()`
// / `.peek()` / `.consume()`, not directly).  `length_ge<5>(refined)`
// evaluates `refined.size() >= 5`, which fails substitution because
// `Refined` exposes no `.size()` accessor.
//
// The correct composition is the `all_of<>` combinator from
// RefinedAlgebra.h:
//
//   Refined<all_of<length_ge<5>, positive>, std::vector<int>>{v};
//
// which evaluates BOTH predicates against the bare T.  Per the §XVI
// alias-discipline rule, accidentally nesting `MinLength` to compose
// refinements must surface a clear concept-violation, NOT a SFINAE
// wall inside the contract's pre-clause.
//
// Mismatch class for this fixture (distinct from companion fixtures):
//   * Companion #1 (Class C — arithmetic .size() missing): an
//     arithmetic type lacks the `.size()` member that the predicate
//     body invokes.
//   * THIS fixture (Class N — nested-Refined composition error): a
//     valid container type wrapped in Refined<> is offered as the
//     outer alias's T parameter.  Refined<>'s public surface
//     deliberately does not expose `.size()` (delegates to .peek() /
//     .value() / .consume()), so the SAME `.size()` substitution
//     failure surfaces at the outer alias's concept gate — but the
//     misuse is a *composition* mistake, not an arithmetic-vs-
//     container mistake.  The two fixtures co-witness the discipline:
//     the alias-surface gate fires regardless of whether the wrong T
//     is a scalar or a poorly-composed-wrapper.
//
// Substring "PredicateInvocableOn" pins the diagnostic — GCC 16 emits
// the concept name in the "constraint requires" line of the
// `MinLength<5, Refined<positive, ...>>{...}` construction site.

#include <crucible/safety/Refined.h>

#include <vector>

int main() {
    using crucible::safety::MinLength;
    using crucible::safety::Refined;
    using crucible::safety::positive;

    // The (correct) inner Refined — wraps a non-empty vector.
    Refined<positive, int> elem{42};
    (void)elem;

    // VIOLATION: trying to nest aliases — outer MinLength<5, ...> over
    // an inner Refined<positive, std::vector<int>>.  The outer
    // alias's `length_ge<5>(c)` body needs `c.size()`, which
    // Refined<> does not provide (intentional — encapsulation).
    // PredicateInvocableOn<length_ge<5>, Refined<positive, std::vector<int>>>
    // is therefore false.
    std::vector<int> v{1, 2, 3, 4, 5};
    Refined<positive, std::vector<int>> inner{v};
    MinLength<5, Refined<positive, std::vector<int>>> bad{inner};
    (void)bad;
    // ERROR: no matching constructor; constraints not satisfied —
    // 'PredicateInvocableOn' [with auto Pred = length_ge<5>,
    //                         T = Refined<positive, vector<int>>]
    return 0;
}
