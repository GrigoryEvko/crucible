// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A wait policy that parks sleeps on a futex after its spin budget, and a
// futex wait blocks the thread.  The foreground context owns no Block, so
// wait_for refuses the policy SpinThenPark and names the gate.  The same call
// with the policy Spin compiles.
//
// Expected diagnostic: no candidate of wait_for, with a note on the gate of
// the wait policy.

#include <fixy/concurrent/EventCount.h>
#include <foundation/effects/Ctx.h>

int main() {
    ::fixy::concurrent::EventCount events{};
    ::fixy::concurrent::wait_for<::fixy::concurrent::SpinThenPark<1000>>(::foundation::effects::testing::foreground(),
                                                                         events, []() noexcept { return true; });
    return 0;
}
