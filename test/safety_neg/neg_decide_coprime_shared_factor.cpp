// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::decide::
// coprime, mismatch class #1: SHARED PRIME FACTOR.
//
// Pins that the predicate correctly rejects a pair sharing a
// common prime factor.  The witness `(6, 9)` is the canonical
// anti-pattern bait: 6 % 9 != 0 (it's 6) AND 9 % 6 != 0 (it's 3),
// so the naive `pre(a % b != 0 && b % a != 0)` check accepts.
// But gcd(6, 9) == 3, so the pair is NOT coprime — the full
// Euclidean algorithm walks one more step (gcd(6, 3) = 3, then
// gcd(3, 0) = 3) and rejects.
//
// In production this bug manifests as: hash-table double-probing
// where the secondary stride and capacity share a factor.  The
// probe sequence visits only `capacity / gcd(stride, capacity)`
// distinct slots before cycling — meaning lookups can fail to
// find an existing key even when free slots exist elsewhere in
// the table.  Subtle, intermittent, hard to debug.  The fix is
// citing this predicate at the boundary: stride must be coprime
// to capacity, full stop.
//
// Anti-pattern targeted: `pre(a % b != 0 && b % a != 0)` —
// one-sided divisibility check that misses any shared prime
// factor smaller than min(a, b).  The fact that this is the
// FIRST anti-pattern an engineer reaches for (because gcd is
// "intuitively" about divisibility) makes it the highest-priority
// fixture target.
//
// Distinct from the companion fixture (zero_pair):
//   * shared_factor (this fixture) — fires on `(6, 9)`.
//     Both nonzero; share factor 3.  Catches the one-sided-
//     modulo anti-pattern.  THE classical coprimality bug
//     class.
//   * zero_pair                    — fires on `(0, 0)`.
//     Degenerate input; gcd(0, 0) is conventionally 0, not 1.
//     Catches the "I forgot the zero edge case" bug class
//     where production code doesn't guard against
//     uninitialized inputs.
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

// (6, 9) — gcd is 3.  Naive `(a % b != 0) && (b % a != 0)`
// accepts: 6 % 9 == 6 (nonzero), 9 % 6 == 3 (nonzero).  Full
// Euclidean rejects: gcd(6, 9) → gcd(6, 3) → gcd(3, 0) = 3.
// CRUCIBLE_PRE's __builtin_trap fires at consteval.
constexpr auto witness = gate(6u, 9u);

}  // namespace

int main() { return 0; }
