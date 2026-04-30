// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-I20 fixture — single-atom rejection (Alloc only).  Pins
// BackgroundThread::run_in_row rejects Row<Alloc>.  An Alloc cap
// alone does NOT imply Bg/IO/Block — the fence demands all four.
//
// Subrow<Row<Bg, Alloc, IO, Block>, Row<Alloc>> = false.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// Subrow<Row<Bg, Alloc, IO, Block>, Row<Alloc>>.

#include <crucible/BackgroundThread.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    ::crucible::BackgroundThread bt;
    bt.run_in_row<eff::Row<eff::Effect::Alloc>>();
    return 0;
}
