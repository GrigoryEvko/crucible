// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-I20 fixture — per-axis missing-atom rejection (missing IO).
// Pins BackgroundThread::run_in_row rejects Row<Bg, Alloc, Block> —
// three of the four required atoms are present, but IO is missing.
// The fence demands ALL four.
//
// Catches a regression where the fence is silently weakened to
// stop charging IO — under the false intuition that the bg drain
// "doesn't really do IO".  But region_ready_cb fires the audit-log
// callback and the federated-cache enqueue, both of which are
// observable IO.  Removing IO from the fence would let callers in
// IO-forbidden contexts (Hot, DetSafe::Pure-bound) drive the bg
// drain — silently violating their own row contract.
//
// Subrow<Row<Bg, Alloc, IO, Block>, Row<Bg, Alloc, Block>> = false.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// Subrow<Row<Bg, Alloc, IO, Block>, Row<Bg, Alloc, Block>>.

#include <crucible/BackgroundThread.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    ::crucible::BackgroundThread bt;
    bt.run_in_row<eff::Row<eff::Effect::Bg, eff::Effect::Alloc,
                            eff::Effect::Block>>();
    return 0;
}
