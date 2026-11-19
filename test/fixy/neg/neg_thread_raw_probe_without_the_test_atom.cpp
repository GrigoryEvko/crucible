// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The raw probe of the thread door does not ask the transfer facts of the
// state that its bodies share, so only a test can call it.  Its second
// context must own Test, which only the testing door gives.  A background
// context owns no Test, so the door refuses the call and names the gate.
//
// Expected diagnostic: no candidate of run_joined_raw_probe, with a note on
// the gate of the second context.

#include <fixy/os/ThreadTasks.h>
#include <foundation/effects/Ctx.h>

int main() {
    namespace eff = ::foundation::effects;
    eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg>> const ctx{eff::testing::bg()};
    auto body = []() noexcept {};
    [[maybe_unused]] auto started = ::fixy::thread::run_joined_raw_probe(ctx, ctx, "a probe with no test authority",
                                                                         ::fixy::thread::ThreadOptions{}, body);
    return 0;
}
