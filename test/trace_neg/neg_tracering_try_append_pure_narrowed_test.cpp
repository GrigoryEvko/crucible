// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// TraceRing::try_append_pure promises a pure caller.  A test runner narrows
// its context to the empty row with in_row, and the narrowed context keeps
// the source of the test runner, which permits Test, Alloc, IO, Block, Nondet
// and Div.  The gate reads the source as well as the row, so the narrowed
// context fails the gate.
//
// Expected diagnostic: CtxHasPureSource of the narrowed context is not
// satisfied.

#include <crucible/TraceRing.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>

namespace eff = ::foundation::effects;

int main() {
    const ::fixy::TestRunnerCtx test{eff::testing::test()};
    crucible::TraceRing ring;
    crucible::TraceRing::Entry e{};
    (void)ring.try_append_pure(test.in_row<eff::Row<>>(), e);
    return 0;
}
