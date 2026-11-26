// Negative-compile test.  This file must fail to compile.
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

#include "ring_ends.h"

namespace eff = ::foundation::effects;

namespace {

void append_from_an_allocating_caller(::fixy::TestRunnerCtx const&) {
    const ::fixy::FixedArray<crucible::TensorMeta, 1> record{};
    crucible::test::with_log_producer([&record](crucible::MetaLog::Producer& log) noexcept {
        (void)crucible::MetaLog::try_append_pure(log, crucible::mint_meta_run(record.view()));
    });
}

}  // namespace

int main() {
    const ::fixy::TestRunnerCtx test{eff::testing::test()};
    append_from_an_allocating_caller(test);
    return 0;
}
