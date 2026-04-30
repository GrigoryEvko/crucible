// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-I20 fixture — single-atom rejection (Bg only).  Pins
// BackgroundThread::run_in_row rejects Row<Bg> — even though Bg is
// THE canonical bg-thread context tag, the fence demands every
// observable effect (Alloc + IO + Block).  Catches a regression
// where the fence is silently weakened to "just Bg present" and
// stops checking the per-effect budget.
//
// Subrow<Row<Bg, Alloc, IO, Block>, Row<Bg>> = false.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// Subrow<Row<Bg, Alloc, IO, Block>, Row<Bg>>.

#include <crucible/BackgroundThread.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    ::crucible::BackgroundThread bt;
    bt.run_in_row<eff::Row<eff::Effect::Bg>>();
    return 0;
}
