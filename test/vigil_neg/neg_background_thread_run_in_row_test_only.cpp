// Negative-compile test.  This file must fail to compile.
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

namespace {

struct Scope final {};

// The handle is a parameter, so the fixture builds no ring and no log.
[[maybe_unused]] void run_in_row_with(::crucible::BackgroundThread::Stopped<Scope>& stopped) {
    const eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test>> ctx{eff::testing::test()};
    stopped.run_in_row(ctx);
}

}  // namespace

int main() { return 0; }
