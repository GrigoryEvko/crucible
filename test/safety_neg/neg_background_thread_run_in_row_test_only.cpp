// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-I20 fixture — orthogonal-row rejection (Test only).  Pins
// BackgroundThread::run_in_row rejects Row<Test>.  Test is the
// test-fixture context tag (poison-byte injection, invariant
// probes) — it is structurally orthogonal to the bg-loop's
// required row.  Catches a regression where test-context callers
// are accepted by accident (the fence demands the SPECIFIC four
// atoms, not just "any tag").
//
// Subrow<Row<Bg, Alloc, IO, Block>, Row<Test>> = false.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// Subrow<Row<Bg, Alloc, IO, Block>, Row<Test>>.

#include <crucible/BackgroundThread.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    ::crucible::BackgroundThread bt;
    bt.run_in_row<eff::Row<eff::Effect::Test>>();
    return 0;
}
