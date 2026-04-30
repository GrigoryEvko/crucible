// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-I20 fixture — per-axis missing-atom rejection (missing Bg).
// Pins BackgroundThread::run_in_row rejects Row<Alloc, IO, Block> =
// STRow — three of the four required atoms are present, but Bg
// (the bg-context tag) is missing.  The fence demands ALL four
// atoms.  This is also the F* alias rejection witness for STRow
// (which equals Row<Block, Alloc, IO>).
//
// Catches a regression where the fence is "fixed" to drop Bg
// because the substitution looked too restrictive — Bg is the
// load-bearing context tag identifying that the call is on the bg
// thread, NOT on the fg hot path.  Removing it would silently
// accept fg callers that hold {Alloc, IO, Block} (which any
// initialization-tier caller has).
//
// Subrow<Row<Bg, Alloc, IO, Block>, Row<Alloc, IO, Block>> = false.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// Subrow<Row<Bg, Alloc, IO, Block>, Row<Alloc, IO, Block>>.

#include <crucible/BackgroundThread.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/FxAliases.h>

namespace eff = ::crucible::effects;

int main() {
    ::crucible::BackgroundThread bt;
    bt.run_in_row<eff::Row<eff::Effect::Alloc, eff::Effect::IO,
                            eff::Effect::Block>>();
    return 0;
}
