// Negative-compile test.  This file must fail to compile.
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

#include "ring_ends.h"

namespace eff = ::foundation::effects;

int main() {
    const ::fixy::TestRunnerCtx test{eff::testing::test()};
    crucible::TraceRing::Entry e{};
    crucible::test::with_ring_producer([&](crucible::TraceRing::Producer& ring) noexcept {
        (void)crucible::TraceRing::try_append_pure(test.in_row<eff::Row<>>(), ring, e);
    });
    return 0;
}
