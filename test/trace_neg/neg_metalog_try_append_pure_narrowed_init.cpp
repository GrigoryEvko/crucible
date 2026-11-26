// Negative-compile test.  This file must fail to compile.
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

#include "ring_ends.h"

namespace eff = ::foundation::effects;

int main() {
    const ::fixy::InitLoadCtx init{eff::testing::init()};
    const ::fixy::FixedArray<crucible::TensorMeta, 1> record{};
    crucible::test::with_log_producer([&](crucible::MetaLog::Producer& log) noexcept {
        (void)crucible::MetaLog::try_append_pure(init.in_row<eff::Row<>>(), log,
                                                 crucible::mint_meta_run(record.view()));
    });
    return 0;
}
