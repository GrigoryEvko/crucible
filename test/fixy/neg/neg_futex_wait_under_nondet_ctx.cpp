// A futex wait is a wait in the kernel, so it carries Block.  A context
// that owns Nondet and no Block can read a clock and end a spin at a
// budget, but it cannot sleep in the kernel, so the wait refuses it.

#include <fixy/os/Futex.h>
#include <foundation/effects/Ctx.h>

namespace eff = ::foundation::effects;

namespace {
using NondetCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Nondet>>;
}  // namespace

int main() {
    NondetCtx const ctx{eff::testing::test()};
    ::fixy::futex::FutexWord word{0};
    [[maybe_unused]] auto ended = ::fixy::futex::wait(ctx, word, 0);
    return 0;
}
