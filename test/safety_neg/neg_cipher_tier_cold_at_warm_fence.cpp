// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a `CipherTier<Cold, T>` value to a function
// whose `requires` clause demands `CipherTier::satisfies<Warm>` —
// the load-bearing rejection for the Cipher::publish_warm
// freshness-guarantee gate.
//
// THE CONCRETE SCENARIO this catches: replay_engine loads a
// historical archive (CipherTier<Cold>) from S3 cold storage,
// and a refactor mistakenly feeds it into a path that expects
// `CipherTier::satisfies<Warm>` — i.e., a freshly-published
// value that has been written to NVMe in the current epoch.
//
// The Augur-drift-attribution machinery and the per-iteration
// memory-plan invalidation logic both assume "if I see a
// CipherTier<Warm>-or-better value, the bytes correspond to
// THIS run's most recent publication."  A Cold-tier value
// (loaded from a 30-day-old archive) silently passing the gate
// would attribute current-epoch drift to historical baselines —
// a recommendations-engine integrity violation.
//
// Lattice direction (CipherTierLattice.h):
//     Cold(weakest) ⊑ Warm ⊑ Hot(strongest)
//
// satisfies<Required> = leq(Required, Self).  For Cold to satisfy
// Warm, we'd need leq(Warm, Cold) — but Warm is STRONGER than
// Cold, so leq(Warm, Cold) is FALSE.  The requires-clause
// rejects the call.
//
// [GCC-WRAPPER-TEXT] — requires-clause rejection of cross-tier flow.

#include <crucible/safety/CipherTier.h>

#include <utility>

using namespace crucible::safety;

// Production-like consumer: Cipher freshness gate that demands
// Warm-or-better tier.  Models the Cipher::publish_warm ⇄
// Augur::attribute_drift / KernelCache::evict-source pattern.
template <typename W>
    requires (W::template satisfies<CipherTierTag_v::Warm>)
static int warm_publish_consumer(W wrapped) noexcept {
    return std::move(wrapped).consume();
}

int main() {
    // Pinned at Cold — origin is replay_engine loading an S3 archive.
    cipher_tier::Cold<int> cold_archive{42};

    // Should FAIL: warm_publish_consumer requires Warm-or-better;
    // cold_archive carries Cold, which is STRICTLY WEAKER than Warm.
    // Without this rejection, historical archives would silently
    // attribute against current-epoch drift signatures, breaking
    // Augur's recommendations-engine integrity contract.
    int result = warm_publish_consumer(std::move(cold_archive));
    return result;
}
