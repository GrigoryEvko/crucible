// Negative compile test.  This file must fail to compile.
//
// The reason of a raw probe of the thread door is a string literal, so the
// call names its probe where a reader sees it.  A text that the program
// builds is no literal, so it gives no reason, and the call does not
// compile.
//
// Expected diagnostic: no conversion of the text to the reason of a probe.

#include <fixy/Ctx.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/core/Text.h>
#include <foundation/effects/Ctx.h>

int main() {
    namespace eff = ::foundation::effects;
    eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg>> const ctx{eff::testing::bg()};
    ::fixy::TestRunnerCtx const test{eff::testing::test()};
    ::foundation::core::TextView const built{"a probe"};
    auto body = []() noexcept {};
    [[maybe_unused]] auto started =
        ::fixy::thread::run_joined_raw_probe(ctx, test, built, ::fixy::thread::ThreadOptions{}, std::move(body));
    return 0;
}
