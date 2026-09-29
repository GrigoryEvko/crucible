// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// BackgroundThread::run_in_row takes a context whose row admits Bg, Alloc,
// IO and Block.  A start-up context claims Init, and no start-up context
// can claim Bg, so the gate refuses it.  With a background context in its
// place, the call compiles.

#include <crucible/BackgroundThread.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

namespace eff = ::foundation::effects;

int main() {
    ::crucible::BackgroundThread bt;
    const eff::ExecCtx<eff::Init, eff::Row<eff::Effect::Init>> ctx{eff::testing::init()};
    bt.run_in_row(ctx);
    return 0;
}
