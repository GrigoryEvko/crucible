// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 2 for fixy-A1-005 (#1547):
// mint_refined<Pred, T>(value) PredicateInvocableOn<Pred, T>
// rejection — predicate-argument-mismatch branch.
//
// Premise: mint_refined is the §XXI authorization point for
// Refined<Pred, T> construction (CLAUDE.md §XXI).
// PredicateInvocableOn<Pred, T> gates "Pred is callable with
// T const& AND returns a value convertible to bool" — the load-
// bearing concept-level soundness check.  Passing a T for which
// Pred's body fails substitution is a type-system category error
// rejected at the concept boundary, NOT a wall of SFINAE deep
// inside the contract's `pre(Pred(v))` clause.
//
// Distinct mismatch class from companion fixture
// neg_mint_refined_predicate_return_mismatch.cpp:
//   * This fixture: predicate not invocable on T (arg-side)
//   * Companion:    predicate returns non-bool-convertible type
// Two independent PredicateInvocableOn failure modes — argument
// vs return type — both must fire to witness the concept covers
// the full predicate-mismatch surface.
//
// Substring "PredicateInvocableOn" pins the diagnostic — GCC 16
// emits the concept name in the "constraint requires" line.

#include <crucible/safety/Refined.h>

namespace {
// A type with no operator> defined and no implicit conversion to
// arithmetic types.  positive's body (`x > decltype(x){0}`) cannot
// be substituted against it: `decltype(x){0}` requires a ctor from
// int, and `x > ...` requires a comparison operator — both absent.
struct UncomparableThing {
    int payload = 0;
};
}  // namespace

int main() {
    using crucible::safety::mint_refined;
    using crucible::safety::positive;

    // Should FAIL: PredicateInvocableOn<positive, UncomparableThing>
    // is false because positive's lambda body cannot be substituted
    // for an UncomparableThing operand.  Concept gate rejects the
    // mint at the call site.
    auto r = mint_refined<positive, UncomparableThing>(UncomparableThing{});
    (void)r;
    return 0;
}
