// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A futex wake serves the waiters of a blocking protocol, so the futex door
// takes a context that owns Block for a wake too.  The foreground context
// owns no Block, so the door refuses the wake and names the gate.
//
// Expected diagnostic: no candidate of wake_all, with a note on the gate of
// the context.

#include <fixy/os/Futex.h>
#include <foundation/effects/Ctx.h>

int main() {
    ::fixy::futex::FutexWord word{0};
    [[maybe_unused]] auto woken = ::fixy::futex::wake_all(::foundation::effects::testing::foreground(), word);
    return 0;
}
