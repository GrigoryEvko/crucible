// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 2 for fixy-A1-005 (#1547):
// mint_sealed_refined<Pred, T>(value) PredicateInvocableOn<Pred, T>
// rejection — predicate-return-shape-mismatch branch.
//
// Premise: same predicate-invocability gate as mint_refined.  The
// concept's back half (`{ Pred(v) } -> std::convertible_to<bool>`)
// fires when Pred returns void / non-bool-convertible — distinct
// mismatch class from the argument-side companion fixture.
// Independent witness from the mint_refined variant of the same
// name because the rejected SYMBOL differs (mint_sealed_refined
// vs mint_refined).

#include <crucible/safety/SealedRefined.h>

namespace {
// Captureless stateless lambda returning void — fails the
// convertible_to<bool> half of PredicateInvocableOn.  Structural
// closure type, usable as an auto NTTP.
inline constexpr auto returns_void_pred =
    [](auto) constexpr noexcept -> void {};
}  // namespace

int main() {
    using crucible::safety::mint_sealed_refined;

    // Should FAIL: PredicateInvocableOn<returns_void_pred, int>
    // is false (void → bool not convertible).  Concept gate
    // rejects the sealed mint.
    auto s = mint_sealed_refined<returns_void_pred, int>(42);
    (void)s;
    return 0;
}
