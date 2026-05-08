// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 2 for safety/Pre.h + safety/Post.h (the foundation
// pair shipped to bypass the GCC 16.1.1 P2900 consteval bypass — see
// memory feedback_crucible_pre_post_macros.md for the diagnosis).
//
// Premise: CRUCIBLE_POST on a function returning a scalar must fire at
// consteval when the postcondition is violated.  Per the probe table,
// shape #6 (`post(r: r > 0)` on scalar return) is one of the 6/7 P2900
// shapes that GCC 16.1.1 silently bypasses at consteval — exactly the
// gap CRUCIBLE_POST closes.
//
// Why this is the load-bearing soundness gate:
//
// Production-side CRUCIBLE_POST sites assert invariants on computed
// results — saturated-arithmetic outputs, content-hash invariants,
// monotone-counter advances.  If CRUCIBLE_POST silently bypassed at
// consteval, the entire post-clause discipline would be invisible to
// neg-compile fixtures: a regression that returned a bad value from a
// `consteval`-eligible computation would compile without complaint, the
// downstream `[[assume]]` would propagate the lie, and the optimizer
// would happily exploit the false invariant.  This fixture nails down
// the consteval-firing guarantee.
//
// Companion fixture: neg_pre_macro_consteval_struct_ref.cpp
//   * That one tests CRUCIBLE_PRE on `T const&` input (the precondition
//     counterpart, Shape #2 in the probe table).
//   * This one tests CRUCIBLE_POST on a scalar return (the postcondition
//     counterpart, Shape #6).
//   * Two distinct mismatch classes per HS14 mandate.
//
// Expected diagnostic: "non-constant condition" / "not a constant
// expression" / "__builtin_trap" — GCC's consteval evaluator hitting
// the non-constexpr trap planted by the if-consteval branch in
// CRUCIBLE_PRE (which CRUCIBLE_POST delegates to).

#include <crucible/safety/Post.h>

#include <cstdint>

namespace {

// The function under test — exact shape of the broken Pattern-C
// production sites (compute_storage_nbytes saturated return,
// fmix_preserves_non_zero invariant, monotonic-step advancement).
//
// Returns x - 1 with the post-clause asserting the result is positive.
// When called with x == 1, the result is 0 → the postcondition is
// violated → CRUCIBLE_POST must fire at consteval.
[[nodiscard]] constexpr int decrement_must_stay_positive(int const x) noexcept {
    int const r = x - 1;
    CRUCIBLE_POST(r, r > 0);   // the contract that MUST fire at consteval
    return r;
}

// This static_assert is the witness: if CRUCIBLE_POST were silently
// bypassed (the GCC 16 P2900 bug behavior on Shape #6), the call
// returns 0 and the static_assert succeeds — silent false-green.
//
// CRUCIBLE_POST plants `if consteval { __builtin_trap(); }` (via
// CRUCIBLE_PRE), which is NOT a constant expression, so the consteval
// evaluator fails with "non-constant condition for static assertion" —
// exactly what we want a neg-compile fixture to surface.
static_assert(decrement_must_stay_positive(1) == 0,
    "CRUCIBLE_POST on a scalar return MUST fire at consteval when the "
    "post-condition is violated.  If this static_assert ever evaluates "
    "successfully, the Post.h consteval-enforcement is broken and every "
    "neg-compile fixture that depends on a contract-firing-at-consteval "
    "post-clause is silently green when it should be red — exactly the "
    "soundness gap that motivated shipping CRUCIBLE_POST in the first "
    "place.");

}  // namespace

int main() { return 0; }
