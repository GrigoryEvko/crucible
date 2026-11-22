// Negative compile test.  This file must fail to compile.
//
// expect_dies starts a child process, and only a test calls it.  A
// background context owns no Test capability, so the concept TestContext
// refuses it at the call.
//
// Expected diagnostic: expect_dies has no viable candidate, and the note
// names the concept.

#include <fixy/check/Check.h>

namespace eff = foundation::effects;

namespace {
using BgDrainCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;
}  // namespace

int main() {
    BgDrainCtx const ctx{eff::testing::bg()};
    fixy::check::expect_dies(ctx, [] noexcept {}, foundation::core::TextView{"text"});
    return 0;
}
