// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// run_beside runs its body on another thread while the continuation runs on
// the calling thread.  The continuation can write each local of the caller,
// also an int that the body reads through a reference to const, and the
// check cannot see that alias.  So the door refuses a body that reads a plain
// int through a reference to const (foundation/Closure.h, rule 8).
//
// Expected diagnostic: the verdict of the door names the field and the cause.

#include <fixy/os/ThreadTasks.h>
#include <foundation/effects/Ctx.h>

int main() {
    namespace eff = ::foundation::effects;
    eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg>> const ctx{eff::testing::bg()};
    int limit = 4;
    int const& read_limit = limit;
    auto body = [&read_limit]() noexcept { return read_limit; };
    auto continuation = [&limit]() noexcept { limit = 5; };
    [[maybe_unused]] auto started =
        ::fixy::thread::run_beside(ctx, ::fixy::thread::ThreadOptions{}, body, continuation);
    return 0;
}
