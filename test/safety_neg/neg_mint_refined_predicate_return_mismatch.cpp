// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 2 for fixy-A1-005 (#1547):
// mint_refined<Pred, T>(value) PredicateInvocableOn<Pred, T>
// rejection — predicate-return-shape-mismatch branch.
//
// Premise: same as companion fixture
// neg_mint_refined_predicate_arg_mismatch.cpp.  The concept also
// gates on `{ Pred(v) } -> std::convertible_to<bool>` — so a
// predicate that compiles fine against T BUT returns a void / non-
// bool-convertible result fails the back half of the concept.
// Distinct mismatch class from the companion: argument side vs
// return side of the concept's two-clause requirement.

#include <crucible/safety/Refined.h>

namespace {
// Stateless lambda that compiles on any T (no arg constraints)
// but returns void — fails `convertible_to<bool>` half of
// PredicateInvocableOn.  Captureless so it qualifies as a
// structural type for use as an auto NTTP (C++20).
inline constexpr auto returns_void_pred =
    [](auto) constexpr noexcept -> void {};
}  // namespace

int main() {
    using crucible::safety::mint_refined;

    // Should FAIL: returns_void_pred(int) returns void, which is
    // NOT convertible to bool.  PredicateInvocableOn<returns_void
    // _pred, int> is false; concept gate rejects the mint.
    auto r = mint_refined<returns_void_pred, int>(42);
    (void)r;
    return 0;
}
