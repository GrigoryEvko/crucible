// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-084 HS14 fixture #4 of 4 for fixy/sync/MemOrderGrant.h:
// `fixy::sync::MemOrder<Order::SeqCst, T>::relax<Relaxed>()` is
// REJECTED — the surface preserves the substrate's strict-up
// rejection at the substrate's `relax<>()` requires-clause.
//
// Why this matters: V-084 surfaces the chain-ordered C++ memory-order
// wrapper.  A regression that drops the substrate's `requires
// (MemOrderLattice::leq(WeakerTag, Tag))` clause — or admits a
// `tighten()`-shaped escape hatch — would let a SeqCst-tier value
// (seq_cst-fenced atomic op) silently CLAIM Relaxed compliance,
// breaking the CLAUDE.md §VI seq_cst ban discipline.  The substrate
// test_safety_mem_order_self_test pins the rejection at the
// substrate; this fixture pins the rejection THROUGH the fixy surface
// so the umbrella refactor cannot drift the discipline.
//
// Distinct mismatch class (HS14 "≥2 distinct mismatch classes"):
// SUBSTRATE-DISCIPLINE PROPAGATION half — the strict-up rejection
// must fire identically when reached via the fixy alias.  Sibling
// `neg_fixy_sync_memorder_tag_to_int_implicit.cpp` exercises the
// SCOPED-ENUM half (enum class → int rejection).
//
// Expected diagnostic: "constraints not satisfied" / "no matching
// function for call to .* relax" / "MemOrderLattice::leq" / "requires
// (MemOrderLattice::leq".

#include <crucible/fixy/sync/MemOrderGrant.h>

namespace fixy_sync = crucible::fixy::sync;

int main() {
    // SeqCst-tier wrapper (the WEAKEST claim in the MemOrder chain —
    // a heavy fence is used).  Per substrate MemOrder.h doc-block:
    //   relax<WeakerTag>() is allowed iff WeakerTag ≤ Tag in the
    //   lattice.  SeqCst is at the bottom (uses heaviest fence,
    //   carries strongest ordering); Relaxed is at the top (claims
    //   no fence needed).  Relaxing UP to Relaxed would CLAIM the
    //   value carried no total-order dependency — false for a
    //   seq_cst-fenced value.
    fixy_sync::MemOrder<fixy_sync::Order::SeqCst, int> seqcst_value{42};

    // STRICT-UP REJECTION: Relaxed > SeqCst → MemOrderLattice::leq(
    // Relaxed, SeqCst) == false → requires-clause fails.
    auto relaxed_up = seqcst_value.template relax<fixy_sync::Order::Relaxed>();
    (void)relaxed_up;
    return 0;
}
