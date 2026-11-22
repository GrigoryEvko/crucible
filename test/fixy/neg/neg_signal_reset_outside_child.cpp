// Negative compile test.  This file must fail to compile.
//
// reset_to_default takes the ForkedChild proof, and only the child door
// builds one, in the child, after the fork (the child door of
// fixy/os/Process.h).  A test that builds the proof itself would change a
// signal disposition of the parent, so the constructor of the proof is
// private.
//
// Expected diagnostic: the constructor of ForkedChild is private within
// this context.

#include <fixy/Ctx.h>
#include <fixy/os/Process.h>
#include <fixy/os/Signal.h>
#include <foundation/effects/Effect.h>

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    fixy::process::ForkedChild const child{};
    fixy::signal::reset_to_default(test, child, fixy::signal::FatalSignal::segmentation_fault);
    return 0;
}
