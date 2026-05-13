// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-I20 fixture — per-axis missing-atom rejection (missing Alloc).
// Pins BackgroundThread::run_in_row rejects Row<Bg, IO, Block> —
// three of the four required atoms are present, but Alloc is
// missing.  The fence demands ALL four.
//
// Catches a regression where the fence "trusts" callers that admit
// IO + Block (under the false assumption that any non-trivial
// effect implies Alloc).  Crucible has caps where IO and Block
// hold but Alloc does not — fully-pinned arena + zero-alloc paths
// — so the explicit Alloc requirement is load-bearing.
//
// Subrow<Row<Bg, Alloc, IO, Block>, Row<Bg, IO, Block>> = false.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// Subrow<Row<Bg, Alloc, IO, Block>, Row<Bg, IO, Block>>.

#include <crucible/BackgroundThread.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    ::crucible::BackgroundThread bt;
    bt.run_in_row<eff::Row<eff::Effect::Bg, eff::Effect::IO,
                            eff::Effect::Block>>();
    return 0;
}
