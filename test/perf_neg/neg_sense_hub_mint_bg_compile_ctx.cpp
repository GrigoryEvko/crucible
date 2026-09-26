// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_sense_hub rejects BgCompileCtx.  That context holds the
// background capability and claims Row<Bg, Alloc, IO>, so it satisfies
// two of the three atoms the gate demands and fails on Block alone.
//
// This is a distinct mismatch class from the ColdInitCtx fixture: the
// capability source is a different one, and the row is short by the
// same atom.  The pair proves the gate reads the wait rather than the
// capability source.  The background load context claims Block on top
// of this row, and the gate admits it.

#include <crucible/perf/SenseHub.h>
#include <fixy/Ctx.h>

int main() {
    auto hub = crucible::perf::mint_sense_hub(::fixy::BgCompileCtx{::foundation::effects::testing::bg()},
                                              ::fixy::InitLoadCtx{::foundation::effects::testing::init()});
    (void)hub;
    return 0;
}
