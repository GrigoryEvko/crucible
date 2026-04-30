// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-I20 fixture — pure-row rejection.  Pins the requires-clause
// on BackgroundThread::run_in_row<CallerRow>.  The template parameter
// must satisfy `Subrow<Row<Bg, Alloc, IO, Block>, CallerRow>`.  A
// caller in a Hot/Pure context (empty row, Row<>) cannot satisfy the
// constraint because {Bg, Alloc, IO, Block} ⊄ {} — the substitution
// must fail loudly with a constraint diagnostic, NOT silently
// proceed.
//
// Why this matters: the 8th-axiom fence on run_in_row prevents fg
// hot-path code from inadvertently driving the bg drain.  Without
// this fence, a refactor that accidentally calls
// bg.run_in_row(...) from a Hot context would compile cleanly AND
// silently inherit Bg + Alloc + IO + Block effects on the fg
// hot path — destroying recording-latency guarantees.  The fence
// catches that drift at substitution time.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// Subrow<Row<Bg, Alloc, IO, Block>, Row<>>.

#include <crucible/BackgroundThread.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    ::crucible::BackgroundThread bt;
    bt.run_in_row<eff::Row<>>();
    return 0;
}
