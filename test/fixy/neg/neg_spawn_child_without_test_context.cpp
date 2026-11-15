// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// spawn_child starts a child process, and only a test starts one.  A
// background context owns no Test capability, so the gate
// CtxFitsChildProcess refuses it at the call.
//
// Expected diagnostic: spawn_child has no viable candidate, and the note
// names the gate.

#include <fixy/os/Process.h>

namespace eff = foundation::effects;

namespace {
using BgDrainCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;
}  // namespace

int main() {
    BgDrainCtx const ctx{eff::testing::bg()};
    auto child = fixy::process::spawn_child(ctx, [] noexcept {});
    return 0;
}
