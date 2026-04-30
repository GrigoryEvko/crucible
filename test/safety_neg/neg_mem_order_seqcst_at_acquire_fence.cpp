// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a `MemOrder<SeqCst, T>` value to a function
// whose `requires` clause demands `MemOrder::satisfies<Acquire>` —
// the load-bearing rejection for the FOUND-G32 production call site
// (AtomicSnapshot::load_mo_pinned and friends).
//
// THE LOAD-BEARING REJECTION FOR FOUND-G32 (MemOrder production
// call site).  AtomicSnapshot::load_mo_pinned returns MemOrder<
// Acquire, T>.  Hot-path consumers that classify themselves as
// Acquire-fence-tolerating REQUIRE at-least-Acquire-tier
// hardware-friendliness from their producers.  A SeqCst-emitting
// site (e.g., a refactor swapped memory_order_acquire for
// memory_order_seq_cst to "make a bug go away") MUST be rejected
// at the Acquire-fence boundary — SeqCst is the WEAKEST
// hardware-friendliness claim (full bidirectional fence on every
// op; mfence on x86; full barrier on ARM), and silently accepting
// it on the hot path turns ~1-3 ns acquire-loads into ~10-30 ns
// fence-bracketed sequences.
//
// Lattice direction (MemOrderLattice.h, lines 60-73):
//     SeqCst(weakest) ⊑ AcqRel ⊑ Release ⊑ Acquire ⊑ Relaxed(strongest)
//
// satisfies<Required> = leq(Required, Self).  For SeqCst to satisfy
// Acquire, we'd need leq(Acquire, SeqCst) — but Acquire is STRICTLY
// HIGHER (more hardware-friendly) than SeqCst, so leq(Acquire,
// SeqCst) is FALSE.  The requires-clause rejects.
//
// Concrete bug-class this catches: a refactor adds
// memory_order_seq_cst to a hot-path atomic op (the well-known
// "make any race go away" bypass).  The op's pinning weakens from
// MemOrder<Acquire, T> to MemOrder<SeqCst, T>.  EVERY hot-path
// consumer's `requires satisfies<Acquire>` rejects it at compile
// time, naming the failed predicate.  The bug never reaches main;
// the test reddens at the PR.  This is the canonical "SeqCst-creep
// hot-path regression" class — today caught by careful review or
// by perf telemetry weeks later (mfences saturate the memory
// ordering buffer); with the wrapper, caught at the type level.
//
// Symmetric matrix-fill fixtures (FOUND-G31) cover wrapper-surface
// rejections (assign / swap / equality / cross-tier mixing).  THIS
// fixture exercises the CONSUMER-FENCE rejection at the production-
// call-site of FOUND-G32 — without it, the most important real-
// world bug class (SeqCst creep on the hot path) is not exercised
// by the neg-compile harness.
//
// [GCC-WRAPPER-TEXT] — requires-clause rejection of cross-tier flow.

#include <crucible/safety/MemOrder.h>

#include <utility>

using namespace crucible::safety;

// Production-like consumer: hot-path Acquire-fence admission gate.
// Models the seqlock-reader / Augur-snapshot-consumer pattern that
// FOUND-G32 production sites flow into.
template <typename W>
    requires (W::template satisfies<MemOrderTag_v::Acquire>)
static int acquire_fence_consumer(W wrapped) noexcept {
    return std::move(wrapped).consume();
}

int main() {
    // Pinned at SeqCst — origin is a site that emits a full-fence
    // atomic op (memory_order_seq_cst).  This is what hot-path call
    // sites MUST reject; SeqCst on the foreground recording path is
    // a 10-30× latency regression vs the canonical Acquire load.
    MemOrder<MemOrderTag_v::SeqCst, int> seqcst_value{42};

    // Should FAIL: acquire_fence_consumer requires satisfies<Acquire>;
    // SeqCst is STRICTLY WEAKER (in the hardware-friendliness lattice)
    // than Acquire, so the constraint fails.  Without this fence, a
    // SeqCst-emitting refactor would silently flow into the hot path,
    // collapsing throughput by an order of magnitude on every read.
    int result = acquire_fence_consumer(std::move(seqcst_value));
    return result;
}
