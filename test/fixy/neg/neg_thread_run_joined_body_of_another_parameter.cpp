// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A body of the thread door takes no argument, or the context of the call.
// A body that takes a parameter of a different type has no argument that
// the door can give, so the door refuses the call.
//
// Expected diagnostic: no candidate of run_joined, with a note on the shape
// of the body.

#include <fixy/os/ThreadTasks.h>
#include <foundation/effects/Ctx.h>

int main() {
    namespace eff = ::foundation::effects;
    eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg>> const ctx{eff::testing::bg()};
    auto body = [](int /*count*/) noexcept {};
    [[maybe_unused]] auto started = ::fixy::thread::run_joined(ctx, ::fixy::thread::ThreadOptions{}, std::move(body));
    return 0;
}
