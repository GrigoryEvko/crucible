// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::decide::
// fmix_preserves_non_zero, mismatch class #1: SEED IS ZERO.
//
// Pins the FIRST clause of the predicate ("seed != 0") at consteval.
// A zero seed makes the predicate return false; CRUCIBLE_PRE fires
// `__builtin_trap()`, which the front-end rejects as
// "non-constant condition".
//
// Witness `(seed = 0, mix_output = 0)` — fmix64(0) is exactly 0
// (the bijection's fixed point), so this pair models the canonical
// case of a caller who forgot to gate seed with a non-zero
// precondition before publishing the resulting hash.
//
// In production this bug manifests as: KernelCache::publish or
// Cipher::store accepts a content_hash of zero into a slot that
// uses 0 as the "not-yet-computed" sentinel.  The call returns
// successfully; subsequent `lookup(content_hash=0)` calls match
// the empty slot's sentinel and falsely report a cache hit.
// Wrong kernel runs.  Production silently produces wrong outputs.
//
// The bug is sneaky because:
//
//   1. The cache is populated with EVERY content_hash collision-
//      free run; a malformed seed only manifests when the structural
//      fingerprint happens to be zero (e.g. an empty op list, or
//      a call site with all zeroed-out ScalarType / DeviceType
//      arguments).  May go undetected for weeks.
//   2. A buggy predicate `pre (mix_output != 0)` (OUTPUT-ONLY) would
//      CORRECTLY REJECT this case (mix=0) but FAIL the companion
//      fixture (mix=non-zero with seed=anything).  Both clauses
//      must be witnessed; this fixture pins clause 1.
//   3. A buggy predicate `pre (seed != 0 || mix != 0)` (DISJUNCTION)
//      would correctly reject this case (both zero) but admits
//      `(seed=0, mix=non-zero)` and `(seed=non-zero, mix=0)`.
//      Always conjunction.
//
// Anti-pattern targeted: per-call-site spelling of fmix's
// non-zero-preservation invariant via single-clause checks.  Specific
// shapes:
//
//   pre (mix_output != 0)
//     // OUTPUT-ONLY — does not witness that the seed was non-zero.
//     // If the caller forgot to gate seed, fmix64(0) == 0 sneaks
//     // through unnoticed.  Companion fixture catches this; this
//     // fixture would NOT (mix=0 here).
//
//   pre (seed != 0 || mix != 0)
//     // DISJUNCTION — admits both single-clause violations.  Use
//     // conjunction.
//
//   pre (h != 0)         // where h is the just-computed mix
//     // OUTPUT-ONLY in a slightly-different spelling.  Same bug.
//
// Distinct from the companion fixture (mix-zero collision):
//   * zero-seed (this fixture)  — `(seed=0, mix=0)`. Pins seed-zero
//     bug class.  Catches OUTPUT-ONLY-style buggy implementations
//     that would still reject this case but not the companion.
//   * mix-zero (companion)      — `(seed=non-zero, mix=0)`. Pins
//     bijection-collision bug class.  Catches SEED-ONLY-style buggy
//     implementations that would still reject this case but not
//     this one.
//
// Together the two fixtures pin TWO ORTHOGONAL bug-class buckets
// that map to non-overlapping buggy implementations: a seed-only
// impl passes #1 fails #2, a mix-only impl passes #2 fails #1.
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

// `seed = 0` — fmix64(0) == 0 as a true bijection invariant, so the
// `(seed=0, mix=0)` pair is the natural witness.  fmix_preserves_non_zero
// rejects because seed is zero; CRUCIBLE_PRE's __builtin_trap fires
// at consteval.
constexpr auto witness = gate(0, 0);

}  // namespace

int main() { return 0; }
