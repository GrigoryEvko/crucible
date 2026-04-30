// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a `HotPath<Cold, T>` value to a function whose
// `requires` clause demands `HotPath::satisfies<Hot>` — the load-
// bearing rejection for the per-op recording site / hot-dispatch
// admission gate (CLAUDE.md §VIII operation-shape table + §IX hot-
// path concurrency rules + 28_04 §4.3.2).
//
// THE LOAD-BEARING REJECTION FOR FOUND-G22 (HotPath production call
// site).  TraceRing::try_append_pinned and MetaLog::try_append_pinned
// return HotPath<Hot, *>.  Hot-dispatch consumers — the per-op
// recording sites budgeted at ~5 ns each — REQUIRE Hot tier.  A
// HotPath<Cold, *> value that originates from a context allowed to
// alloc / syscall / block (Cipher::flush_cold, Canopy gossip writer,
// Forge background compile) MUST be rejected at the foreground hot-
// path boundary.  Cold-tier work is hundreds of ns to milliseconds —
// two-to-five orders of magnitude above the per-op recording budget.
//
// Lattice direction (HotPathLattice.h):
//     Cold(weakest) ⊑ Warm ⊑ Hot(strongest)
//
// satisfies<Required> = leq(Required, Self).  For Cold to satisfy
// Hot, we'd need leq(Hot, Cold) — but Cold is STRICTLY WEAKER than
// Hot, so leq(Hot, Cold) is FALSE.  The requires-clause rejects the
// call.
//
// Concrete bug-class this catches: a refactor that adds a printf,
// a logging-channel send, or a kernel-mediated transition to the
// per-op recording path.  Today caught by review or by a 5-15%
// throughput regression spotted in CI bench three iterations later;
// with this fence in place, the same refactor weakens the function's
// declared HotPath tier from Hot to Cold, and EVERY hot-dispatch
// caller's `requires satisfies<Hot>` rejects it at compile time.
// The bug never reaches main; the test reddens at the PR.
//
// Symmetric-with-companion fixtures: this is the Cold-rejection-at-
// Hot-fence variant of the existing matrix-fill set:
//   neg_hot_path_relax_to_stronger.cpp        (relax<> tightening)
//   neg_hot_path_cross_tier_assign.cpp        (cross-tier value flow)
//   neg_hot_path_cross_tier_equality.cpp      (cross-tier compare)
//   neg_hot_path_cross_tier_swap.cpp          (cross-tier swap)
//
// The earlier matrix exercises the tag-set-CARRIER level rejections.
// THIS fixture exercises the CONSUMER-FENCE level rejection — the
// production-side gate that the FOUND-G22 production call sites
// (TraceRing::try_append_pinned / MetaLog::try_append_pinned) flow
// into.  Without this fixture, the consumer-fence rejection is not
// exercised by the neg-compile harness, leaving an undetected hole
// where a future Hot-fence regression could land silently.
//
// [GCC-WRAPPER-TEXT] — requires-clause rejection of cross-tier flow.

#include <crucible/safety/HotPath.h>

#include <utility>

using namespace crucible::safety;

// Production-like consumer: foreground hot-dispatch admission gate
// that demands Hot tier.  Models the TraceRing::try_append_pinned
// ⇄ Vigil::dispatch_op consumer pattern — every per-op recording
// site of the foreground path runs under this gate.
template <typename W>
    requires (W::template satisfies<HotPathTier_v::Hot>)
static int hot_dispatch_consumer(W wrapped) noexcept {
    return std::move(wrapped).consume();
}

int main() {
    // Pinned at Cold — origin is a context where alloc / syscall /
    // block is admissible (e.g. Cipher::flush_cold, Canopy gossip
    // writer, Forge background-compile worker).
    HotPath<HotPathTier_v::Cold, int> cold_value{42};

    // Should FAIL: hot_dispatch_consumer requires Hot; cold_value
    // carries Cold, which is STRICTLY WEAKER than Hot.  Without the
    // requires-clause fence, a value originating from a blocking /
    // syscalling / IO-doing context would silently flow into the
    // ~5 ns per-op recording site, blowing the structural latency
    // contract for the entire foreground recording pipeline.
    int result = hot_dispatch_consumer(std::move(cold_value));
    return result;
}
