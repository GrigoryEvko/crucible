// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 2 for safety/Pre.h (the foundation header
// shipped to bypass the GCC 16.1.1 P2900 consteval bypass — see
// memory feedback_crucible_pre_post_macros.md for the full diagnosis).
//
// Premise: CRUCIBLE_PRE on a function taking `T const&` of a struct
// must fire at consteval when the predicate is violated.  The
// canonical mismatch class — calling the function with a `constexpr`
// instance whose state violates the precondition — is the exact case
// that GCC's native `pre()` clause silently bypasses.  This fixture
// proves CRUCIBLE_PRE catches it.
//
// Why this is the load-bearing soundness gate:
//
// Production migrations (cog::content_hash, mimic::CogMimic::
// cog_kernel_cache_key, and the ~10 Pattern-C sites still pending)
// all rely on this exact shape — a constexpr-eligible function
// taking a struct by const-ref, with a precondition that reads
// through the struct.  If this fixture ever compiles successfully,
// every production neg-compile fixture that depends on consteval
// PRE-firing is silently green when it should be red — exactly the
// soundness gap that surfaced as the cog_neg fixture failure.
//
// Companion fixture: neg_post_macro_consteval_scalar_return.cpp
//   * That one tests CRUCIBLE_POST on a scalar return (the
//     post-clause counterpart).  Distinct mismatch class (post-
//     condition rather than pre-condition).
//   * This one tests CRUCIBLE_PRE on a struct const-ref input
//     (the pre-clause counterpart).  Distinct mismatch class.
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate,
// each demonstrating a distinct mismatch class.
//
// Expected diagnostic: "non-constant condition" / "not a constant
// expression" / "__builtin_trap" — GCC's consteval evaluator hitting
// the non-constexpr trap planted by the if-consteval branch.

#include <crucible/safety/Pre.h>

#include <cstdint>

namespace {

struct S {
    std::uint64_t lo = 0;
    [[nodiscard]] constexpr bool nz() const noexcept { return lo != 0; }
};

// The function under test — exact shape of the broken Pattern-C
// production sites (cog::content_hash, CogMimic::*, MerkleDag::*).
[[nodiscard]] constexpr std::uint64_t under_test(S const& s) noexcept {
    CRUCIBLE_PRE(s.nz());   // the contract that MUST fire at consteval
    return s.lo;
}

// Default-constructed S has lo == 0 → s.nz() == false → contract
// violated.  Calling under_test(ZERO) at consteval MUST poison the
// surrounding expression.
constexpr S ZERO{};

// This static_assert is the witness: if CRUCIBLE_PRE were silently
// bypassed (the GCC 16 P2900 bug behavior), under_test(ZERO) would
// return 0 and the static_assert would pass — silent false-green.
//
// CRUCIBLE_PRE plants `if consteval { __builtin_trap(); }` which is
// NOT a constant expression, so the consteval evaluator fails with
// "non-constant condition for static assertion" — exactly what we
// want a neg-compile fixture to surface.
static_assert(under_test(ZERO) == 0,
    "CRUCIBLE_PRE on T const& MUST fire at consteval when the "
    "predicate is violated.  If this static_assert ever evaluates "
    "successfully, the Pre.h consteval-enforcement is broken and "
    "every neg-compile fixture that depends on contract-firing-at-"
    "consteval is silently green when it should be red — exactly "
    "the soundness gap that motivated shipping CRUCIBLE_PRE in the "
    "first place.");

}  // namespace

int main() { return 0; }
