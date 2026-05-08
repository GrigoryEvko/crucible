// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::decide::
// fmix_preserves_non_zero, mismatch class #2: MIX OUTPUT IS ZERO
// (despite seed being non-zero).
//
// Pins the SECOND clause of the predicate ("mix_output != 0") at
// consteval.  A non-zero seed paired with a zero mix output models
// the case where fmix64's bijection property is VIOLATED — a
// hypothetical buggy hash family that collides non-zero inputs to
// zero.  fmix_preserves_non_zero rejects; CRUCIBLE_PRE fires
// `__builtin_trap()`, which the front-end rejects as
// "non-constant condition".
//
// Witness `(seed = 0xDEADBEEF, mix_output = 0)`.  The real fmix64
// CANNOT produce this pairing (it's a bijection that maps 0↔0); we
// construct it manually to demonstrate that the predicate would
// catch a future hash family that BREAKS the bijection.
//
// In production this bug manifests as: someone replaces fmix64
// with a custom mixer (e.g. for performance, or to remove a
// transitive dependency), the new mixer happens to collapse
// certain non-zero inputs to zero.  Without the second clause,
// these collisions silently pass the publish gate and corrupt the
// `0 = sentinel, non-0 = valid` discipline of KernelCache /
// Cipher head_ / MerkleDag content_hash / RegionNode merkle_hash.
//
// Concretely: a Forge IR fingerprint produces seed = X (non-zero
// by construction); the bad mixer collapses to 0; publish accepts
// the hash; subsequent lookup matches the empty-slot sentinel; a
// stale or wrong kernel runs.  Production silently produces wrong
// outputs.
//
// The bug is sneaky because:
//
//   1. Real-world hash family changes are rare but DO happen
//      (vendor-neutral migrations, perf tuning, regulatory crypto
//      requirements).  The CI must catch the bijection break at
//      the predicate site, not in some downstream "wrong cache hit"
//      symptom three weeks later.
//   2. A buggy predicate `pre (seed != 0)` (SEED-ONLY) would
//      CORRECTLY REJECT the companion fixture (seed=0) but FAIL
//      this case (seed=non-zero, mix=0).  Both clauses must be
//      witnessed; this fixture pins clause 2.
//   3. A buggy predicate `pre (seed != 0 || mix != 0)` (DISJUNCTION)
//      would accept this case (seed clause holds).  Always
//      conjunction.
//
// Anti-pattern targeted: trusting the bijection without a runtime
// witness.  Specific shapes:
//
//   pre (seed != 0)
//     // SEED-ONLY — trusts the bijection statically.  Defeats the
//     // hash-family-change safety net.  Companion fixture catches
//     // this; this fixture would NOT (seed=non-zero here).
//
//   pre (seed != 0 || mix != 0)
//     // DISJUNCTION — admits both single-clause violations.  Use
//     // conjunction.
//
// Distinct from the companion fixture (zero-seed):
//   * zero-seed (companion)     — `(seed=0, mix=0)`. Pins seed-zero
//     bug class.  Catches OUTPUT-ONLY-style buggy implementations.
//   * mix-zero (this fixture)   — `(seed=non-zero, mix=0)`. Pins
//     bijection-collision bug class.  Catches SEED-ONLY-style
//     buggy implementations.
//
// Together the two fixtures pin TWO ORTHOGONAL bug-class buckets
// that map to non-overlapping buggy implementations: a seed-only
// impl passes #1 fails #2, a mix-only impl passes #2 fails #1.
//
// Note: this fixture does NOT compute fmix64 of 0xDEADBEEF (that
// would yield a non-zero result and the predicate would accept).
// The fixture's whole point is to construct an `(seed, mix)` pair
// that violates the bijection property — which can't happen with
// the actual fmix64 — to prove that the predicate would catch a
// hypothetical buggy mixer.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "non-constant condition" / "not a constant expression" /
//   "__builtin_trap" / "call to non-constexpr function".

#include <crucible/safety/Decide.h>
#include <crucible/safety/Pre.h>

#include <cstdint>

namespace {

[[nodiscard]] constexpr bool gate(std::uint64_t seed,
                                  std::uint64_t mix_output) noexcept {
    CRUCIBLE_PRE(crucible::decide::fmix_preserves_non_zero(seed, mix_output));
    return true;
}

// `seed = 0xDEADBEEF, mix_output = 0` — non-zero seed, but mix
// output is artificially zero.  This pair CANNOT come from the real
// fmix64 (bijective), but a buggy hash family could produce it.
// fmix_preserves_non_zero rejects because mix is zero; CRUCIBLE_PRE's
// __builtin_trap fires at consteval.
constexpr auto witness = gate(0xDEADBEEFULL, 0);

}  // namespace

int main() { return 0; }
