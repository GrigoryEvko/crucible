// Negative compile test.  This file must fail to compile.
//
// A spin deadline reads the counter of the core, a source of
// nondeterminism.  So SpinDeadline::after takes the bare tag of Nondet, and
// only a context whose row holds Nondet gives it.  The foreground context
// holds no Nondet, so it gives no tag, and the hot path takes no deadline.
//
// Expected diagnostic: no candidate of extract_bare_from_ctx for the
// foreground context.

#include <fixy/os/SpinBudget.h>
#include <foundation/effects/Ctx.h>

int main() {
    [[maybe_unused]] auto const deadline = ::fixy::time::SpinDeadline::after(
        ::foundation::effects::extract_bare_from_ctx<::foundation::effects::Effect::Nondet>(
            ::foundation::effects::testing::foreground()),
        ::fixy::time::SpinBudget{1000});
    return 0;
}
