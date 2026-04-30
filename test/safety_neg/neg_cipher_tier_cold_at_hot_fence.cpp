// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a `CipherTier<Cold, T>` value to a function
// whose `requires` clause demands `CipherTier::satisfies<Hot>` —
// the load-bearing rejection for the Keeper hot-tier reincarnation
// gate (CRUCIBLE.md §L14, 28_04 §4.3.7).
//
// THE LOAD-BEARING REJECTION FOR THE no-cold-on-hot-reshard
// discipline.  Cipher::publish_cold returns CipherTier<Cold, T>;
// hot-tier consumers (Keeper reincarnation admission gate, zero-loss
// reshard fan-in) require Hot tier.  A CipherTier<Cold, T> value
// coming from S3/GCS storage MUST be rejected at the hot-tier
// boundary — Cold tier carries minutes-of-latency for total-cluster-
// failure recovery, NEVER appropriate at single-node-failure
// reincarnation where the hot path has microseconds-to-milliseconds
// budget at most.
//
// Lattice direction (CipherTierLattice.h):
//     Cold(weakest) ⊑ Warm ⊑ Hot(strongest)
//
// satisfies<Required> = leq(Required, Self).  For Cold to satisfy
// Hot, we'd need leq(Hot, Cold) — but Cold is STRICTLY WEAKER than
// Hot, so leq(Hot, Cold) is FALSE.  The requires-clause rejects
// the call.
//
// Concrete bug-class this catches: a refactor that introduces a
// "convenience" overload accepting a raw ContentHash and re-wrapping
// internally as CipherTier<Cold, ContentHash> — bypassing the Hot
// fence.  Without this fixture, S3-backed values would silently
// flow into Keeper's hot-reshard fan-in, blocking the entire cluster
// recovery path on minutes-of-S3-GET latency at the worst possible
// moment (a peer-Relay just died, fellow Relays are racing to
// reshard from RAM).
//
// [GCC-WRAPPER-TEXT] — requires-clause rejection of cross-tier flow.

#include <crucible/safety/CipherTier.h>

#include <utility>

using namespace crucible::safety;

// Production-like consumer: hot-reshard admission gate that demands
// Hot tier.  Models the Cipher::publish_hot ⇄ Keeper::reincarnate
// pattern.
template <typename W>
    requires (W::template satisfies<CipherTierTag_v::Hot>)
static int hot_reshard_consumer(W wrapped) noexcept {
    return std::move(wrapped).consume();
}

int main() {
    // Pinned at Cold — origin is an S3 cold archive load.  This is
    // what hot-tier admission gates MUST reject.
    cipher_tier::Cold<int> cold_value{42};

    // Should FAIL: hot_reshard_consumer requires Hot;
    // cold_value carries Cold, which is STRICTLY WEAKER than Hot.
    // Without the requires-clause fence, S3-backed values would
    // silently flow into the recovery-time fan-in, blocking the
    // hot path on minutes-of-S3-GET latency.
    int result = hot_reshard_consumer(std::move(cold_value));
    return result;
}
