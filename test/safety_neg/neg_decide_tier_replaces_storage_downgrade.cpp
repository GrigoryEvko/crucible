// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::decide::
// tier_replaces, mismatch class #1: STORAGE-RESIDENCY DOWNGRADE.
//
// Pins that the predicate correctly rejects a strictly-weaker
// candidate replacing a strictly-stronger requirement on the
// CipherTierTag chain lattice — i.e., a Cold-tier candidate (S3
// blob, ~minutes recovery from total cluster failure) cannot
// substitute for a slot that demands Hot-tier (replicated in
// another Relay's RAM, ~μs recovery via gossip).
//
// Witness `(candidate=Cold, required=Hot)`.  Per the chain-lattice
// convention (see algebra/lattices/CipherTierLattice.h §"Direction
// convention"), stronger guarantee = HIGHER ordinal:
//
//     Cold (0)  ⊑  Warm (1)  ⊑  Hot (2)
//
// `tier_replaces(c, r)` ≡ `to_underlying(c) >= to_underlying(r)`.
// On `(Cold, Hot)` the underlying compare is 0 >= 2 = false →
// CRUCIBLE_PRE fires `__builtin_trap()` at consteval, which the
// front-end rejects as "non-constant condition".
//
// In production this bug manifests as: a KernelCache read path that
// declares its slot expects Hot-tier persistence (so the Vigil's
// recovery target is RAID-replicated μs) but silently admits a
// candidate originating from cold-tier S3 blob storage.  On Relay
// failure the recovery latency silently shifts from μs to minutes —
// the Cipher tiers documented in CRUCIBLE.md §L14 stop conveying
// the SLA they exist to encode.  runtime observer's drift-attribution dispatch
// also breaks: a "cold-tier S3 latency" mis-classified as a hot-
// path issue routes the wrong recommendation back to the Keeper.
//
// The bug is sneaky because:
//
//   1. Both sides of the comparison ARE valid CipherTierTag values
//      — neither is a sentinel; type-system rejection alone won't
//      catch it.
//   2. A buggy predicate using `<=` instead of `>=` (sign reversed)
//      would FALSELY ACCEPT this case: 0 <= 2 is true.  The fixture
//      pins the polarity.
//   3. A buggy predicate that name-compares ("Cold" < "Hot"
//      alphabetically) would also accept it.  The fixture pins
//      ordinal-based semantics.
//
// Anti-pattern targeted: per-call-site spelling of the chain-tier
// comparison instead of citing this procedure.  Specific shapes:
//
//   pre (static_cast<int>(candidate) <= static_cast<int>(required))
//     // SIGN REVERSED — admits Cold→Hot
//
//   pre (candidate == required)
//     // Identity-only — refuses every legal upgrade (Hot→Cold etc.)
//
//   pre (candidate.satisfies<required>())
//     // Per-lattice method scattered across call sites; doesn't
//     // unify with the seven sister chain lattices.
//
// Distinct from the companion fixture (determinism downgrade):
//   * storage      (this fixture) — fires on the storage-residency
//     axis (CipherTierTag).  Production consequence: SLA recovery
//     target silently downgrades.
//   * determinism (companion)     — fires on the determinism-budget
//     axis (DetSafeTier, a 7-element chain).  Production
//     consequence: bit-exact replay invariant breaks; runtime drift
//     attribution can't separate runs; cross_vendor_step_invariant
//     CI test reddens.
//
// Both fixtures pin the SAME structural rule (correct comparator
// direction on chain-lattice ordering) but on TWO DIFFERENT
// production bug classes.  A reviewer fixing one must verify the
// other still holds — both bug families route through the
// lattice-agnostic procedure, so a single regression in
// tier_replaces breaks both.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "non-constant condition" / "not a constant expression" /
//   "__builtin_trap" / "call to non-constexpr function".

#include <crucible/algebra/lattices/CipherTierLattice.h>
#include <crucible/safety/Decide.h>
#include <crucible/safety/Pre.h>

namespace {

namespace cl = crucible::algebra::lattices;

[[nodiscard]] constexpr bool gate(
    cl::CipherTierTag candidate,
    cl::CipherTierTag required
) noexcept {
    CRUCIBLE_PRE(crucible::decide::tier_replaces(candidate, required));
    return true;
}

// `(candidate=Cold, required=Hot)` — strictly-weaker provider for a
// strictly-stronger requirement.  Per chain order Cold (0) is below
// Hot (2); tier_replaces rejects; CRUCIBLE_PRE's __builtin_trap
// fires at consteval.
constexpr auto witness = gate(cl::CipherTierTag::Cold,
                              cl::CipherTierTag::Hot);

}  // namespace

int main() { return 0; }
