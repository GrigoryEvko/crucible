// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a `ResidencyHeat<Cold, T>` value to a function
// whose `requires` clause demands `ResidencyHeat::satisfies<Hot>` —
// the load-bearing rejection for the per-op recording site / hot-
// dispatch admission gate (CRUCIBLE.md §L4 + §L2).
//
// THE LOAD-BEARING REJECTION FOR THE no-cold-cache-on-hot-path
// discipline.  KernelCache::lookup_l1 returns ResidencyHeat<Hot, *>;
// hot-dispatch consumers (per-op recording sites budgeted at ~5 ns)
// require Hot tier.  A ResidencyHeat<Cold, *> value coming from
// L3 (compiled-bytes archive, S3-backed) MUST be rejected at the
// hot-path boundary — Cold-tier access is hundreds of ns to
// milliseconds (DRAM at best, disk-backed at worst), two-to-five
// orders of magnitude above the per-call shape budget.
//
// Lattice direction (ResidencyHeatLattice.h):
//     Cold(weakest) ⊑ Warm ⊑ Hot(strongest)
//
// satisfies<Required> = leq(Required, Self).  For Cold to satisfy
// Hot, we'd need leq(Hot, Cold) — but Cold is STRICTLY WEAKER
// than Hot, so leq(Hot, Cold) is FALSE.  The requires-clause
// rejects the call.
//
// Concrete bug-class this catches: a refactor that introduces a
// "convenience" overload accepting a CompiledKernel* raw and re-
// wrapping internally as ResidencyHeat<Cold, *> — bypassing the
// L1 fence.  Without this fixture, L3-loaded values would silently
// flow into TraceRing/Vigil per-op recording sites budgeted at
// ~5 ns per dispatch, breaking the structural latency contract
// for the entire foreground recording pipeline.
//
// [GCC-WRAPPER-TEXT] — requires-clause rejection of cross-tier flow.

#include <crucible/safety/ResidencyHeat.h>

#include <utility>

using namespace crucible::safety;

// Production-like consumer: hot-dispatch admission gate that demands
// Hot tier.  Models the KernelCache::lookup_l1 ⇄ Vigil::dispatch_op
// pattern.
template <typename W>
    requires (W::template satisfies<ResidencyHeatTag_v::Hot>)
static int hot_dispatch_consumer(W wrapped) noexcept {
    return std::move(wrapped).consume();
}

int main() {
    // Pinned at Cold — origin is KernelCache::lookup_l3 (S3 archive).
    residency_heat::Cold<int> cold_value{42};

    // Should FAIL: hot_dispatch_consumer requires Hot; cold_value
    // carries Cold, which is STRICTLY WEAKER than Hot.  Without the
    // requires-clause fence, S3-backed kernel pointers would silently
    // flow into the foreground dispatch path, blowing the ~5 ns
    // per-op shape budget by 100-1000×.
    int result = hot_dispatch_consumer(std::move(cold_value));
    return result;
}
