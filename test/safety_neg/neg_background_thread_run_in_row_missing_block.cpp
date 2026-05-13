// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-I20 fixture — per-axis missing-atom rejection (missing
// Block).  Pins BackgroundThread::run_in_row rejects Row<Bg, Alloc,
// IO> — three of the four required atoms are present, but Block is
// missing.  The fence demands ALL four.
//
// Catches a regression where the fence is silently weakened to
// stop charging Block — under the false intuition that bg is
// "always blocked" and an explicit Block atom is redundant.  But
// the SPSC drain spin-pauses + the OS scheduler parks std::thread
// — both observable blocking effects.  Removing Block from the
// fence would let callers in non-blocking contexts (Pure, ST
// without Block) drive the bg drain.
//
// Subrow<Row<Bg, Alloc, IO, Block>, Row<Bg, Alloc, IO>> = false.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// Subrow<Row<Bg, Alloc, IO, Block>, Row<Bg, Alloc, IO>>.

#include <crucible/BackgroundThread.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    ::crucible::BackgroundThread bt;
    bt.run_in_row<eff::Row<eff::Effect::Bg, eff::Effect::Alloc,
                            eff::Effect::IO>>();
    return 0;
}
