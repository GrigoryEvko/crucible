// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-I20 fixture — single-atom rejection (Block only).  Pins
// BackgroundThread::run_in_row rejects Row<Block> = DivRow.  Block
// alone does NOT imply Bg/Alloc/IO — the fence demands all four.
// This rejection is also the F* alias rejection witness for DivRow
// (which equals Row<Block>).
//
// Subrow<Row<Bg, Alloc, IO, Block>, Row<Block>> = false.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// Subrow<Row<Bg, Alloc, IO, Block>, Row<Block>>.

#include <crucible/BackgroundThread.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/FxAliases.h>

namespace eff = ::crucible::effects;

int main() {
    ::crucible::BackgroundThread bt;
    bt.run_in_row<eff::Row<eff::Effect::Block>>();
    return 0;
}
