// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// MetaLog::try_append_pure reads the row of the context of the caller, and
// the gate IsPure asks that the row is empty.  A context whose row holds
// Alloc fails the gate, because the pure append allocates nothing and a
// caller that allocates is not a hot-path caller.
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
    (void)log.try_append_pure(test.in_row<eff::Row<eff::Effect::Alloc>>(), record.view());
    return 0;
}
