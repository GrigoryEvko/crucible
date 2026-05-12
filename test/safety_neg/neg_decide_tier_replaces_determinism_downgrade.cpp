// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::decide::
// tier_replaces, mismatch class #2: DETERMINISM-BUDGET DOWNGRADE.
//
// Pins that the predicate correctly rejects a strictly-weaker
// candidate replacing a strictly-stronger requirement on the
// DetSafeTier chain lattice — i.e., an EntropyRead-tier candidate
// (kernel touches /dev/urandom or hardware RNG) cannot substitute
// for a slot that demands Pure-tier (function strictly of declared
// inputs, replay-deterministic).
//
// Witness `(candidate=EntropyRead, required=Pure)`.  Per the chain-
// lattice convention (see algebra/lattices/DetSafeLattice.h),
// stronger guarantee = HIGHER ordinal:
//
//     NonDeterministicSyscall (0)  ⊑  FilesystemMtime (1)  ⊑
//       EntropyRead (2)            ⊑  WallClockRead (3)    ⊑
//       MonotonicClockRead (4)     ⊑  PhiloxRng (5)        ⊑
//       Pure (6)
//
// `tier_replaces(c, r)` ≡ `to_underlying(c) >= to_underlying(r)`.
// On `(EntropyRead, Pure)` the underlying compare is 2 >= 6 = false
// → CRUCIBLE_PRE fires `__builtin_trap()` at consteval, which the
// front-end rejects as "non-constant condition".
//
// In production this bug manifests as: a Forge Phase E kernel slot
// that declares its body Pure (the recipe registry pins
// NumericalRecipe with replay-deterministic floats) but silently
// admits an implementation that reads `/dev/urandom` or calls
// `RAND_bytes`.  Consequences:
//
//   1. The bit_exact_replay_invariant CI test reddens — same input,
//      different output across two runs of the same Vigil.
//      DetSafe axiom #8 (`same(inputs) ⇒ same(outputs)`) is broken.
//   2. cross_vendor_step_invariant CI fails — different vendors
//      reach different /dev/urandom values, breaking the cross-
//      vendor numerics CI claim from MIMIC.md §41.
//   3. runtime observer's drift-attribution loses its ground truth — every
//      replay-comparison shows "drift" even when no real drift
//      exists, masking actual hardware/kernel-version drifts that
//      runtime observation is supposed to surface.
//   4. Federation breaks — a deterministic kernel published to a
//      peer org's KernelCache produces different bytes than the
//      publisher; Cipher's cross-org replay no longer agrees.
//
// The bug is sneaky because:
//
//   1. Both sides ARE valid DetSafeTier values — type system alone
//      does not catch it.
//   2. The kernel may APPEAR to work — `/dev/urandom` succeeds; the
//      first run produces plausible output.  The breakage surfaces
//      only on replay, hours or days later, when the second run's
//      bytes diverge.
//   3. A buggy predicate using `==` (identity-only) would fail the
//      Pure→Pure positive case AND falsely accept this — an off-
//      by-one in the strictness direction.  The fixture pins the
//      reflexive-and-non-strict semantics.
//
// Anti-pattern targeted: per-call-site spelling of the determinism
// admission instead of citing this procedure.  Specific shapes:
//
//   pre (kernel.det_tier == declared_recipe.det_tier)
//     // Identity-only — refuses Pure→PhiloxRng (legal upgrade,
//     // strictly stronger candidate satisfies weaker requirement).
//
//   pre (declared_recipe.allows_entropy() ||
//        !kernel.reads_entropy())
//     // Negation-soup; flips polarity under refactor.
//
//   pre (recipe.det_tier_satisfies(kernel.det_tier))
//     // Per-recipe method; doesn't unify with the six sister
//     // chain lattices (CipherTier, HotPath, NumericalTier,
//     // ResidencyHeat, AllocClass, Wait).
//
// Distinct from the companion fixture (storage downgrade):
//   * storage      (companion)    — fires on the storage-residency
//     axis (CipherTierTag, 3-element chain).  Production
//     consequence: SLA recovery target silently downgrades.
//   * determinism (this fixture)  — fires on the determinism-budget
//     axis (DetSafeTier, 7-element chain).  Production
//     consequence: bit-exact replay invariant breaks; runtime drift
//     attribution can't separate runs.
//
// Both fixtures pin the SAME structural rule (correct comparator
// direction on chain-lattice ordering) but on TWO DIFFERENT
// production bug classes spanning DIFFERENT lattice cardinalities
// (3 vs 7).  Together they exercise tier_replaces' lattice-
// agnosticism: a regression in the predicate breaks both.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "non-constant condition" / "not a constant expression" /
//   "__builtin_trap" / "call to non-constexpr function".

#include <crucible/algebra/lattices/DetSafeLattice.h>
#include <crucible/safety/Decide.h>
#include <crucible/safety/Pre.h>

namespace {

namespace cl = crucible::algebra::lattices;

[[nodiscard]] constexpr bool gate(
    cl::DetSafeTier candidate,
    cl::DetSafeTier required
) noexcept {
    CRUCIBLE_PRE(crucible::decide::tier_replaces(candidate, required));
    return true;
}

// `(candidate=EntropyRead, required=Pure)` — strictly-weaker
// provider for a strictly-stronger requirement.  Per chain order
// EntropyRead (2) is below Pure (6); tier_replaces rejects;
// CRUCIBLE_PRE's __builtin_trap fires at consteval.
constexpr auto witness = gate(cl::DetSafeTier::EntropyRead,
                              cl::DetSafeTier::Pure);

}  // namespace

int main() { return 0; }
