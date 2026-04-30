// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a `ResidencyHeat<Cold, T>` value to a function
// whose `requires` clause demands `ResidencyHeat::satisfies<Warm>` —
// the load-bearing rejection for L2 federation freshness gates.
//
// THE CONCRETE SCENARIO this catches: a refactor that loads a
// kernel from L3 (compiled-bytes archive, S3-backed cold tier)
// and feeds it into a path that expects L2 freshness — e.g., the
// per-vendor-family federation publish path that reads the L2
// residence as a freshness signal for sibling-Relay invalidation.
//
// Cold-tier kernels are by design ARCHIVE entries: they may be
// stale relative to a vendor-family L2 publication.  Treating a
// Cold-tier load as a Warm-fresh value would silently broadcast
// stale entries into the federation, breaking the consistency
// guarantee that downstream Forge re-compile decisions depend on
// (a Forge worker that sees "L2-fresh kernel for this content+row"
// would skip recompilation; if that signal was a Cold-tier lie,
// the worker would commit stale code into a fresh ExecutionPlan).
//
// Lattice direction (ResidencyHeatLattice.h):
//     Cold(weakest) ⊑ Warm ⊑ Hot(strongest)
//
// satisfies<Required> = leq(Required, Self).  For Cold to satisfy
// Warm, we'd need leq(Warm, Cold) — but Warm is STRONGER than
// Cold, so leq(Warm, Cold) is FALSE.  The requires-clause
// rejects the call.
//
// [GCC-WRAPPER-TEXT] — requires-clause rejection of cross-tier flow.

#include <crucible/safety/ResidencyHeat.h>

#include <utility>

using namespace crucible::safety;

// Production-like consumer: L2-freshness gate that demands
// Warm-or-better tier.  Models the KernelCache::publish_l2 ⇄
// federation-broadcast pattern.
template <typename W>
    requires (W::template satisfies<ResidencyHeatTag_v::Warm>)
static int warm_freshness_consumer(W wrapped) noexcept {
    return std::move(wrapped).consume();
}

int main() {
    // Pinned at Cold — origin is KernelCache::lookup_l3 (S3 archive).
    residency_heat::Cold<int> cold_archive{42};

    // Should FAIL: warm_freshness_consumer requires Warm-or-better;
    // cold_archive carries Cold, which is STRICTLY WEAKER than Warm.
    // Without this rejection, L3 archive entries would silently
    // claim L2-freshness, breaking the federation invalidation
    // contract that downstream Forge recompile decisions trust.
    int result = warm_freshness_consumer(std::move(cold_archive));
    return result;
}
