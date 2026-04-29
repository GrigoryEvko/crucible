// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling ResidencyHeat<WeakerTier, T>::relax<StrongerTier>()
// when StrongerTier > WeakerTier in the ResidencyHeatLattice.
//
// THE LOAD-BEARING REJECTION FOR THE KernelCache L1 working-set
// DISCIPLINE (CRUCIBLE.md §L2 + §L15).  Without it, a value
// sourced from L3 (or DRAM) could be re-typed as L1-resident and
// silently flow into a KernelCache fast-path lookup, defeating
// the per-call shape budget — L3 cache miss ~hundreds of ns vs
// L1 hit ~ns.
//
// Concrete bug-class this catches: a refactor that loosened the
// requires-clause guarding relax<>() — specifically, a slip from
// `ResidencyHeatLattice::leq(WeakerTier, Tier)` to a permissive
// form — would silently allow a Cold-tier value to claim Hot
// residency.  The dispatcher's L1-only admission gate would then
// admit cold-cache reads into the fast path, breaking the
// working-set heat-tracking story Augur depends on.
//
// Lattice direction: Hot is at the TOP (fastest, ~ns L1 access);
// Cold is at the BOTTOM (slowest, ~hundreds of ns L3/DRAM).
// Going DOWN (Hot → Warm → Cold) is allowed.  Going UP is FORBIDDEN.
//
// [GCC-WRAPPER-TEXT] — requires-clause rejection on relax<>().

#include <crucible/safety/ResidencyHeat.h>

using namespace crucible::safety;

int main() {
    // Pinned at Cold — bytes derive from L3 cache or DRAM.  This
    // is what KernelCache L1 fast-path admission MUST reject; the
    // relax<> below is the bug-introduction path the wrapper
    // fences.
    ResidencyHeat<ResidencyHeatTag_v::Cold, int> cold_value{42};

    // Should FAIL: relax<Warm> on a Cold-pinned wrapper.  The
    // requires-clause `ResidencyHeatLattice::leq(Warm, Cold)` is
    // FALSE — Warm is above Cold in the chain — so the relax<>
    // overload is excluded.  Without this fence, L3-resident
    // values could claim L2 residency and silently enter the
    // KernelCache warm-lookup path, breaking CRUCIBLE.md §L2.
    auto warm_claim = std::move(cold_value).relax<ResidencyHeatTag_v::Warm>();
    return warm_claim.peek();
}
