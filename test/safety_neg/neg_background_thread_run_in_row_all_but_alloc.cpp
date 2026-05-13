// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-I20-AUDIT fixture — saturation-minus-one rejection
// (missing Alloc).  At cardinality 5, with every atom EXCEPT
// Alloc present, the fence STILL rejects.  Maximum-cardinality
// rejection witness for the Alloc axis.
//
// Subrow<Row<Bg, Alloc, IO, Block>, Row<Bg, IO, Block, Init,
//   Test>> = false.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// Subrow<Row<Bg, Alloc, IO, Block>, Row<Bg, IO, Block, Init,
//   Test>>.

#include <crucible/BackgroundThread.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    ::crucible::BackgroundThread bt;
    bt.run_in_row<eff::Row<
        eff::Effect::Bg,    eff::Effect::IO, eff::Effect::Block,
        eff::Effect::Init,  eff::Effect::Test>>();
    return 0;
}
