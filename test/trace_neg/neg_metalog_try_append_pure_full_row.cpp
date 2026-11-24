// Negative-compile test.  This file must fail to compile.
//
// MetaLog::try_append_pure reads the row of the context of the caller, and
// the gate IsPure asks that the row is empty.  The context of the load stage
// of the background thread holds the widest row that a background context
// can hold: Bg, Alloc, IO, Block and Nondet.  It fails the gate.
//
// Expected diagnostic: IsPure of the row of the context is not satisfied.

#include <crucible/MetaLog.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>

#include "ring_ends.h"

namespace eff = ::foundation::effects;

int main() {
    const ::fixy::BgLoadCtx load{eff::testing::bg()};
    const ::fixy::FixedArray<crucible::TensorMeta, 1> record{};
    crucible::test::with_log_producer([&](crucible::MetaLog::Producer& log) noexcept {
        (void)crucible::MetaLog::try_append_pure(load, log, record.view());
    });
    return 0;
}
