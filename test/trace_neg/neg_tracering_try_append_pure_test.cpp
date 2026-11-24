// Negative-compile test.  This file must fail to compile.
//
// TraceRing::try_append_pure reads the row of the context of the caller, and
// the gate IsPure asks that the row is empty.  A context whose row holds
// Test is the context of a test runner, which does not stand in for the
// foreground thread of the record path, so it fails the gate.
//
// Expected diagnostic: IsPure of the row of the context is not satisfied.

#include <crucible/TraceRing.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>

#include "ring_ends.h"

namespace eff = ::foundation::effects;

int main() {
    const ::fixy::TestRunnerCtx test{eff::testing::test()};
    crucible::TraceRing::Entry e{};
    crucible::test::with_ring_producer([&](crucible::TraceRing::Producer& ring) noexcept {
        (void)crucible::TraceRing::try_append_pure(test.in_row<eff::Row<eff::Effect::Test>>(), ring, e);
    });
    return 0;
}
