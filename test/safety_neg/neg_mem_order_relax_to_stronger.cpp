// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling MemOrder<WeakerTag, T>::relax<StrongerTag>()
// when StrongerTag > WeakerTag in the MemOrderLattice.
//
// THE LOAD-BEARING REJECTION FOR THE CLAUDE.md §VI seq_cst BAN.
// Without it, a value sourced from a SeqCst-fenced atomic op
// could be re-typed as AcqRel-bound (or Relaxed) and silently flow
// into a hot-path atomic call site, defeating the per-call shape
// budget — seq_cst on x86 drains the store buffer + global RFO
// (~30-100ns) vs AcqRel's local LOCK CMPXCHG (~5-10ns).
//
// Concrete bug-class this catches: a refactor that loosened the
// requires-clause guarding relax<>() — specifically, a slip from
// `MemOrderLattice::leq(WeakerTag, Tag)` to a permissive form —
// would silently allow a SeqCst-tier value (carrying total-order-
// fence semantics) to claim AcqRel compliance.  The dispatcher's
// hot-path admission gate (per 28_04 §6.4) would then admit the
// value into a TraceRing CAS site, silently introducing a global
// RFO on the foreground path.
//
// Lattice direction: Relaxed is at the TOP (cheapest, no fence);
// SeqCst is at the BOTTOM (most expensive, total-order fence).
// Going DOWN (Relaxed → Acquire → Release → AcqRel → SeqCst) is
// allowed — stronger no-fence claim trivially serves weaker
// requirement.  Going UP is FORBIDDEN — would CLAIM more no-fence
// discipline than the source provides.
//
// [GCC-WRAPPER-TEXT] — requires-clause rejection on relax<>().

#include <crucible/safety/MemOrder.h>

using namespace crucible::safety;

int main() {
    // Pinned at SeqCst — bytes derive from a sequentially-consistent
    // atomic op (the most expensive C++ memory ordering).  This is
    // what hot-path call sites MUST reject; the relax<> below is the
    // bug-introduction path the wrapper fences.
    MemOrder<MemOrderTag_v::SeqCst, int> seqcst_value{42};

    // Should FAIL: relax<AcqRel> on a SeqCst-pinned wrapper.  The
    // requires-clause `MemOrderLattice::leq(AcqRel, SeqCst)` is
    // FALSE — AcqRel is above SeqCst in the chain — so the relax<>
    // overload is excluded.  Without this fence, a seq-cst-fenced
    // value could claim AcqRel compliance and silently enter a
    // hot-path atomic call site, breaking CLAUDE.md §VI discipline.
    auto acqrel_claim = std::move(seqcst_value).relax<MemOrderTag_v::AcqRel>();
    return acqrel_claim.peek();
}
