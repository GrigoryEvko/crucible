// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::decide::
// coprime, mismatch class #2: DEGENERATE ZERO PAIR.
//
// Pins that the predicate correctly rejects the degenerate input
// `(0, 0)`.  By convention gcd(0, 0) is 0 (not 1), so the pair
// is NOT coprime.  Rejecting this case is essential because
// production code that hand-rolls the Euclidean algorithm
// without a zero guard:
//
//   * crashes via division-by-zero in the very first modular
//     step (% 0 is UB), or
//   * silently returns 0 (a typical "convention" some libraries
//     pick), which the comparison `gcd == 1` then rejects — but
//     the implementation has divided by zero already.
//
// factorization_eq's first line is `if (a == 0 && b == 0) return
// false;` — short-circuiting BEFORE any modular operation.  This
// fixture pins that the short-circuit fires at a CRUCIBLE_PRE-
// gated boundary: the call site that passes `(0, 0)` does NOT
// produce UB, does NOT crash, and does NOT silently accept.  The
// predicate returns false; CRUCIBLE_PRE's __builtin_trap fires.
//
// In production this bug manifests as: an uninitialized config
// where a hash-table secondary stride and capacity are both 0
// (e.g. before the table is set up, or after a reset gone wrong).
// Without this fixture, the bug surfaces at runtime as a SIGFPE
// in the divisibility check inside the gcd implementation —
// stack-deep, hard to reproduce.  With it, the boundary contract
// rejects at construction time.
//
// Anti-pattern targeted: hand-rolled Euclidean without zero
// guard.  Specific shapes:
//
//   while (b != 0) { auto t = a % b; a = b; b = t; }
//   return a == 1;        // ← runs `0 % 0` if both inputs zero
//
//   // OR libstdc++'s `std::gcd(0, 0)` which returns 0; the
//   // caller's `== 1` comparison then "correctly" rejects, but
//   // the production cite has zero diagnostic value vs cite
//   // of decide::coprime which names the failure class.
//
// Distinct from the companion fixture (shared_factor):
//   * shared_factor — fires on `(6, 9)` with gcd 3.  Both
//     nonzero, share a factor.  Catches one-sided-modulo bug.
//   * zero_pair (this fixture) — fires on `(0, 0)`.  Degenerate
//     input.  Catches missing-zero-guard / uninitialized-config
//     bug class.  Without the predicate's explicit zero short-
//     circuit, the call would UB.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "non-constant condition" / "not a constant expression" /
//   "__builtin_trap" / "call to non-constexpr function".

#include <crucible/safety/Decide.h>
#include <crucible/safety/Pre.h>

#include <cstdint>

namespace {

[[nodiscard]] constexpr uint32_t gate(uint32_t stride, uint32_t capacity) noexcept {
    CRUCIBLE_PRE(crucible::decide::coprime<uint32_t>(stride, capacity));
    return stride;
}

// (0, 0) — degenerate input.  gcd(0, 0) is conventionally 0;
// coprime is false.  The predicate's first branch short-circuits
// before any modular operation.  CRUCIBLE_PRE's __builtin_trap
// fires at consteval.
constexpr auto witness = gate(0u, 0u);

}  // namespace

int main() { return 0; }
