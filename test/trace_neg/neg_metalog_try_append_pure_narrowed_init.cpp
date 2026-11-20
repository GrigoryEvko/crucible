// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// MetaLog::try_append_pure promises a pure caller.  A startup step narrows its
// init context to the empty row with in_row, and the narrowed context keeps
// the init source, which permits Init, Alloc, IO, Block, Nondet and Div.  The
// gate reads the source as well as the row, so the narrowed context fails the
// gate.
//
// Expected diagnostic: CtxHasPureSource of the narrowed context is not
// satisfied.

#include <crucible/MetaLog.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>

namespace eff = ::foundation::effects;

int main() {
    const ::fixy::InitLoadCtx init{eff::testing::init()};
    crucible::MetaLog log{eff::testing::test().alloc};
    const ::fixy::FixedArray<crucible::TensorMeta, 1> record{};
    (void)log.try_append_pure(init.in_row<eff::Row<>>(), record.view());
    return 0;
}
