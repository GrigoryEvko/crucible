// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 2 for fixy-A1-005 (#1547):
// mint_sealed_refined<Pred, T>(value) PredicateInvocableOn<Pred, T>
// rejection — predicate-argument-mismatch branch.
//
// Premise: mint_sealed_refined is the §XXI authorization point for
// SealedRefined<Pred, T> construction.  Same PredicateInvocableOn
// gate as mint_refined / Refined / SealedRefined ctors — so the
// argument-side failure mode (Pred not callable on T) must fire
// identically.  Independent witness from the mint_refined fixture
// of the same name because the rejected SYMBOL differs
// (mint_sealed_refined vs mint_refined).
//
// Distinct mismatch class from companion fixture
// neg_mint_sealed_refined_predicate_return_mismatch.cpp:
//   * This fixture: predicate not invocable on T (arg-side)
//   * Companion:    predicate returns non-bool-convertible type
//
// Substring "PredicateInvocableOn" pins the diagnostic.

#include <crucible/safety/SealedRefined.h>

namespace {
// Same shape as the mint_refined arg-mismatch fixture: a type
// with no operator> and no implicit conversion, so positive's
// substitution fails.
struct UncomparableThing {
    int payload = 0;
};
}  // namespace

int main() {
    using crucible::safety::mint_sealed_refined;
    using crucible::safety::positive;

    // Should FAIL: PredicateInvocableOn<positive, UncomparableThing>
    // is false; concept gate rejects the sealed mint at the call site.
    auto s = mint_sealed_refined<positive, UncomparableThing>(
        UncomparableThing{});
    (void)s;
    return 0;
}
