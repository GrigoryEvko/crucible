// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a `CipherTier<Warm, T>` value to a function
// whose `requires` clause demands `CipherTier::satisfies<Hot>` —
// the second-most-load-bearing rejection for the Keeper hot-tier
// reincarnation gate (CRUCIBLE.md §L14, 28_04 §4.3.7).
//
// THE CONCRETE SCENARIO this catches: a reincarnation path uses
// Cipher::publish_warm (NVMe-backed) where it MUST use
// Cipher::publish_hot (RAM-replicated).  The Warm-pinned value
// claims durability via NVMe; the hot-reshard fan-in expects a
// value already-replicated in peer RAM (recovery in microseconds,
// not milliseconds).  Without this rejection, a refactor that
// "consolidates" publish_hot into publish_warm "for simplicity"
// silently degrades recovery latency by 100-1000× under
// single-node-failure pressure.
//
// Lattice direction (CipherTierLattice.h):
//     Cold(weakest) ⊑ Warm ⊑ Hot(strongest)
//
// satisfies<Required> = leq(Required, Self).  For Warm to satisfy
// Hot, we'd need leq(Hot, Warm) — but Hot is STRONGER than Warm,
// so leq(Hot, Warm) is FALSE.  The requires-clause rejects the
// call.
//
// The neg_cipher_tier_cold_at_hot_fence fixture catches the
// MOST EXTREME case (Cold→Hot, three tier levels apart).  This
// fixture catches the ONE-TIER-OFF case — the more subtle
// refactor bug where the developer thought "Warm is close enough
// to Hot" and missed the failure-domain semantics.
//
// [GCC-WRAPPER-TEXT] — requires-clause rejection of cross-tier flow.

#include <crucible/safety/CipherTier.h>

#include <utility>

using namespace crucible::safety;

template <typename W>
    requires (W::template satisfies<CipherTierTag_v::Hot>)
static int hot_reshard_consumer(W wrapped) noexcept {
    return std::move(wrapped).consume();
}

int main() {
    // Pinned at Warm — origin is Cipher::publish_warm (NVMe write).
    cipher_tier::Warm<int> warm_value{42};

    // Should FAIL: hot_reshard_consumer requires Hot;
    // warm_value carries Warm, which is STRICTLY WEAKER than Hot.
    // Without the requires-clause fence, NVMe-backed values would
    // silently flow into the reincarnation hot path, blocking on
    // disk IO instead of completing in RAM-replication latency.
    int result = hot_reshard_consumer(std::move(warm_value));
    return result;
}
