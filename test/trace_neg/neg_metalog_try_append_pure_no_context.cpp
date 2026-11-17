// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A caller that holds the context of a test runner, whose row holds Alloc,
// IO and Block, calls MetaLog::try_append_pure and names no row.  A gate
// whose row has a default admits such a caller, because the default row is
// empty whatever the row of the caller is.  The gate takes its row from the
// context, and no form with no context exists.
//
// Expected diagnostic: no try_append_pure takes the records alone.

#include <crucible/MetaLog.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>

namespace eff = ::foundation::effects;

namespace {

void append_from_an_allocating_caller(::fixy::TestRunnerCtx const&, crucible::MetaLog& log) {
    const ::fixy::FixedArray<crucible::TensorMeta, 1> record{};
    (void)log.try_append_pure(record.view());
}

}  // namespace

int main() {
    const ::fixy::TestRunnerCtx test{eff::testing::test()};
    crucible::MetaLog log{eff::testing::test().alloc};
    append_from_an_allocating_caller(test, log);
    return 0;
}
