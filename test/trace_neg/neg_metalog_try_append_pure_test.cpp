// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// MetaLog::try_append_pure reads the row of the context of the caller, and
// the gate IsPure asks that the row is empty.  A context whose row holds
// Test is the context of a test runner, which does not stand in for the
// foreground thread of the record path, so it fails the gate.
//
// Expected diagnostic: IsPure of the row of the context is not satisfied.

#include <crucible/MetaLog.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>

namespace eff = ::foundation::effects;

int main() {
    const ::fixy::TestRunnerCtx test{eff::testing::test()};
    crucible::MetaLog log{eff::testing::test().alloc};
    const ::fixy::FixedArray<crucible::TensorMeta, 1> record{};
    (void)log.try_append_pure(test.in_row<eff::Row<eff::Effect::Test>>(), record.view());
    return 0;
}
