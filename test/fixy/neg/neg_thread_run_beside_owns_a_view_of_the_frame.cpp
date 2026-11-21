// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// run_beside runs its body on another thread while the continuation runs on
// the calling thread.  A body that owns a View of const ints of the frame
// reads the ints while the continuation can write them.  A View of const int
// is Sendable, but it is not SendableBeside (foundation/Transfer.h), so the
// door refuses the body (foundation/Closure.h, rule 8).
//
// Expected diagnostic: the verdict of the door names the field and the cause.

#include <fixy/os/ThreadTasks.h>
#include <foundation/core/Array.h>
#include <foundation/effects/Ctx.h>

int main() {
    namespace eff = ::foundation::effects;
    eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg>> const ctx{eff::testing::bg()};
    ::foundation::core::FixedArray<int, 2> limits{};
    ::foundation::core::FixedArray<int, 2> const& read_limits = limits;
    auto body = [reads = read_limits.view()]() noexcept { return reads.size(); };
    auto continuation = [&limits]() noexcept {
        limits.with_element(::foundation::core::index<0>(), [](int& limit) noexcept { limit = 6; });
    };
    [[maybe_unused]] auto started =
        ::fixy::thread::run_beside(ctx, ::fixy::thread::ThreadOptions{}, body, continuation);
    return 0;
}
