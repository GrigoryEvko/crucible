// Negative-compile test.  This file must fail to compile.
//
// MetaLog::try_append_pure reads the row of the context of the caller, and
// the gate IsPure asks that the row is empty.  A context whose row holds Bg
// is the context of the background thread, which drains the log and never
// calls the producer side, so it fails the gate.
//
// Expected diagnostic: IsPure of the row of the context is not satisfied.

#include <crucible/MetaLog.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>

#include "ring_ends.h"

namespace eff = ::foundation::effects;

int main() {
    const ::fixy::BgDrainCtx bg{eff::testing::bg()};
    const ::fixy::FixedArray<crucible::TensorMeta, 1> record{};
    crucible::test::with_log_producer([&](crucible::MetaLog::Producer& log) noexcept {
        (void)crucible::MetaLog::try_append_pure(bg.in_row<eff::Row<eff::Effect::Bg>>(), log, record.view());
    });
    return 0;
}
