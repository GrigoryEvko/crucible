// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling CipherTier<WeakerTier, T>::relax<StrongerTier>()
// when StrongerTier > WeakerTier in the CipherTierLattice.
//
// THE LOAD-BEARING REJECTION FOR THE Cipher persistence-tier
// DISCIPLINE (CRUCIBLE.md §L14).  Without it, a value sourced from
// S3/Cold storage could be re-typed as Hot/Warm and silently flow
// into a Keeper hot-tier reshard admission gate, defeating the
// recovery-time-latency contract — S3 GET ~minutes vs RAM-replicated
// fellow Relay ~zero-cost.
//
// Concrete bug-class this catches: a refactor that loosened the
// requires-clause guarding relax<>() — specifically, a slip from
// `CipherTierLattice::leq(WeakerTier, Tier)` to a permissive form
// — would silently allow a Cold-tier value to claim Warm or Hot
// compliance.  The Keeper's hot-tier reshard gate would then admit
// S3-bearing reads into the recovery-time hot path, blocking on
// minutes of disk IO at the worst possible moment (cluster-failure
// recovery fan-in).
//
// Lattice direction: Hot is at the TOP (fastest recovery, ~μs);
// Cold is at the BOTTOM (slowest recovery, ~minutes).  Going DOWN
// (Hot → Warm → Cold) is allowed.  Going UP is FORBIDDEN.
//
// [GCC-WRAPPER-TEXT] — requires-clause rejection on relax<>().

#include <crucible/safety/CipherTier.h>

using namespace crucible::safety;

int main() {
    // Pinned at Cold — bytes derive from S3/GCS durable storage.
    // This is what Keeper's hot-tier reshard gate MUST reject; the
    // relax<> below is the bug-introduction path the wrapper fences.
    CipherTier<CipherTierTag_v::Cold, int> cold_value{42};

    // Should FAIL: relax<Warm> on a Cold-pinned wrapper.  The
    // requires-clause `CipherTierLattice::leq(Warm, Cold)` is
    // FALSE — Warm is above Cold in the chain — so the relax<>
    // overload is excluded.  Without this fence, S3-bearing values
    // could claim Warm-tier compliance and silently enter the
    // Keeper warm-publish path, breaking CRUCIBLE.md §L14.
    auto warm_claim = std::move(cold_value).relax<CipherTierTag_v::Warm>();
    return warm_claim.peek();
}
