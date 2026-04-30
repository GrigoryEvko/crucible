// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-I20 fixture — single-atom rejection (IO only).  Pins
// BackgroundThread::run_in_row rejects Row<IO>.  IO alone does NOT
// imply Bg/Alloc/Block — the fence demands all four.
//
// Subrow<Row<Bg, Alloc, IO, Block>, Row<IO>> = false.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// Subrow<Row<Bg, Alloc, IO, Block>, Row<IO>>.

#include <crucible/BackgroundThread.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    ::crucible::BackgroundThread bt;
    bt.run_in_row<eff::Row<eff::Effect::IO>>();
    return 0;
}
