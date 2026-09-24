// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-084 HS14 fixture #2 of 4 for fixy/sync/WaitGrant.h:
// `fixy::sync::Wait<Strategy::Block, T>::relax<SpinPause>()` is
// REJECTED — the surface preserves the substrate's strict-up
// rejection at the substrate's `relax<>()` requires-clause.
//
// Why this matters: V-084 surfaces the chain-ordered wait-strategy
// wrapper.  A regression that drops the substrate's `requires
// (WaitLattice::leq(WeakerStrategy, Strategy))` clause — or admits a
// `tighten()`-shaped escape hatch — would let a Block-tier value
// (futex / blocking syscall) silently CLAIM SpinPause compliance,
// breaking the CLAUDE.md §IX.5 hot-path waiter discipline.  The
// substrate test_safety_wait_self_test pins the rejection at the
// substrate; this fixture pins the rejection THROUGH the fixy surface
// so the umbrella refactor cannot drift the discipline.
//
// Distinct mismatch class (HS14 "≥2 distinct mismatch classes"):
// SUBSTRATE-DISCIPLINE PROPAGATION half — the strict-up rejection
// must fire identically when reached via the fixy alias.  Sibling
// `neg_fixy_sync_wait_strategy_to_int_implicit.cpp` exercises the
// SCOPED-ENUM half (enum class → int rejection).
//
// Expected diagnostic: "constraints not satisfied" / "no matching
// function for call to .* relax" / "WaitLattice::leq" / "requires
// (WaitLattice::leq".

#include <crucible/fixy/sync/WaitGrant.h>

namespace fixy_sync = crucible::fixy::sync;

int main() {
    // Block-tier wrapper (the WEAKEST claim — blocking syscall is
    // permitted).  Per substrate Wait.h doc-block:
    //   relax<WeakerStrategy>() is allowed iff WeakerStrategy ≤
    //   Strategy in the lattice.  Block is at the bottom; nothing
    //   is below Block.  Relaxing UP to SpinPause CLAIMS the value
    //   was produced under SpinPause discipline — which is false.
    fixy_sync::Wait<fixy_sync::Strategy::Block, int> block_value{42};

    // STRICT-UP REJECTION: SpinPause > Block → WaitLattice::leq(
    // SpinPause, Block) == false → requires-clause fails.
    auto relaxed_up = block_value.template relax<fixy_sync::Strategy::SpinPause>();
    (void)relaxed_up;
    return 0;
}
