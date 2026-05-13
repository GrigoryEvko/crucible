// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-I20-AUDIT fixture — saturation-minus-one rejection
// (missing Bg).  At cardinality 5, with every atom of the OS
// universe except Bg present, the fence STILL rejects because Bg
// is required.  Catches a regression where the fence is silently
// widened to "any row of cardinality ≥ 4" (size-based vs
// membership-based check).
//
// Subrow<Row<Bg, Alloc, IO, Block>, Row<Alloc, IO, Block, Init,
//   Test>> = false — Bg ∉ caller.
//
// This is the maximum-cardinality rejection witness for the Bg
// axis: even with every other Effect atom present, missing Bg
// alone is enough to fire the fence.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// Subrow<Row<Bg, Alloc, IO, Block>, Row<Alloc, IO, Block, Init,
//   Test>>.

#include <crucible/BackgroundThread.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    ::crucible::BackgroundThread bt;
    bt.run_in_row<eff::Row<
        eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block,
        eff::Effect::Init,  eff::Effect::Test>>();
    return 0;
}
