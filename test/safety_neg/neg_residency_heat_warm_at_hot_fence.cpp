// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a `ResidencyHeat<Warm, T>` value to a function
// whose `requires` clause demands `ResidencyHeat::satisfies<Hot>` —
// the second-most-load-bearing rejection for the per-op recording
// site / Vigil hot-dispatch admission gate.
//
// THE CONCRETE SCENARIO this catches: a refactor that pulls a
// kernel from L2 (per-vendor-family federation, KernelCache::
// lookup_l2) and feeds it into the hot foreground dispatch path
// expecting L1.  L2 lookups touch a separate physical store;
// even when warm-cached they cost tens-of-ns vs L1's ~5 ns.
// "One-tier-off" is the most common refactor bug — the developer
// sees that the Warm tier is "close to Hot" and misses the
// per-call latency contract.
//
// Lattice direction (ResidencyHeatLattice.h):
//     Cold(weakest) ⊑ Warm ⊑ Hot(strongest)
//
// satisfies<Required> = leq(Required, Self).  For Warm to satisfy
// Hot, we'd need leq(Hot, Warm) — but Hot is STRONGER than Warm,
// so leq(Hot, Warm) is FALSE.  The requires-clause rejects the
// call.
//
// The companion neg_residency_heat_cold_at_hot_fence fixture
// catches the EXTREME case (Cold→Hot, two tier levels apart, S3-
// backed minutes-of-latency).  This fixture catches the SUBTLE
// case (Warm→Hot, one tier off, tens-of-ns penalty per call).
// Subtlety means the runtime regression is harder to spot in
// perf telemetry — exactly why the static fence is load-bearing.
//
// [GCC-WRAPPER-TEXT] — requires-clause rejection of cross-tier flow.

#include <crucible/safety/ResidencyHeat.h>

#include <utility>

using namespace crucible::safety;

template <typename W>
    requires (W::template satisfies<ResidencyHeatTag_v::Hot>)
static int hot_dispatch_consumer(W wrapped) noexcept {
    return std::move(wrapped).consume();
}

int main() {
    // Pinned at Warm — origin is KernelCache::lookup_l2 (per-vendor
    // federation, IR003* lookup).
    residency_heat::Warm<int> warm_value{42};

    // Should FAIL: hot_dispatch_consumer requires Hot;
    // warm_value carries Warm, which is STRICTLY WEAKER than Hot.
    // Without the requires-clause fence, L2-fetched kernel pointers
    // would silently flow into the foreground dispatch path,
    // paying tens-of-ns L2 access penalty per call against a ~5 ns
    // budget.
    int result = hot_dispatch_consumer(std::move(warm_value));
    return result;
}
