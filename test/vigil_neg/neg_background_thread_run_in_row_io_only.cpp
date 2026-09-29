// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// BackgroundThread::run_in_row takes a context whose row admits Bg, Alloc,
// IO and Block.  This background context claims IO alone, so the gate
// refuses it.  With the row widened to all four atoms, the call compiles.

#include <crucible/BackgroundThread.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

namespace eff = ::foundation::effects;

int main() {
    ::crucible::BackgroundThread bt;
    const eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::IO>> ctx{eff::testing::bg()};
    bt.run_in_row(ctx);
    return 0;
}
