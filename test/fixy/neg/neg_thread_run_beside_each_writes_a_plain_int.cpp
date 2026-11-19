// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// run_beside_each calls one body on many threads while the continuation runs
// on the calling thread.  A body that writes a plain int of the caller
// through a reference races with each other thread and with the
// continuation, so the door refuses it (foundation/Closure.h, rule 8).
//
// Expected diagnostic: the verdict of the door names the field and the cause.

#include <fixy/os/ThreadTasks.h>
#include <foundation/effects/Ctx.h>

int main() {
    namespace eff = ::foundation::effects;
    eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg>> const ctx{eff::testing::bg()};
    int count = 0;
    auto body = [&count](::fixy::thread::BesideIndex /*index*/) noexcept { ++count; };
    auto continuation = []() noexcept {};
    [[maybe_unused]] auto started = ::fixy::thread::run_beside_each(
        ctx, ::fixy::thread::ThreadOptions{}, ::fixy::thread::BesideCount<2>::check(2).expect("2 is at most 2"), body,
        continuation);
    return 0;
}
