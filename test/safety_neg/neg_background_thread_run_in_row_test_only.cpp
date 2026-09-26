// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// BackgroundThread::run_in_row takes a context whose row admits Bg, Alloc,
// IO and Block.  A test context claims Test, and no test context can claim
// Bg, so the gate refuses it.  With a background context in its place, the
// call compiles.

#include <crucible/BackgroundThread.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

namespace eff = ::foundation::effects;

int main() {
    ::crucible::BackgroundThread bt;
    const eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test>> ctx{eff::testing::test()};
    bt.run_in_row(ctx);
    return 0;
}
