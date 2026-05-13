// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-I20 fixture — orthogonal-row rejection (Init only).  Pins
// BackgroundThread::run_in_row rejects Row<Init>.  Init is the
// initialization-context tag (Vigil construction, Keeper start) —
// it is structurally orthogonal to the bg-loop's required row.
// Catches a regression where any "non-Pure" context is accepted
// (the fence demands the SPECIFIC four atoms, not just any
// non-emptiness).
//
// Subrow<Row<Bg, Alloc, IO, Block>, Row<Init>> = false.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// Subrow<Row<Bg, Alloc, IO, Block>, Row<Init>>.

#include <crucible/BackgroundThread.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    ::crucible::BackgroundThread bt;
    bt.run_in_row<eff::Row<eff::Effect::Init>>();
    return 0;
}
