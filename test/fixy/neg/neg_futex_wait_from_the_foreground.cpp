// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A futex wait blocks the thread, so the futex door takes a context that
// owns Block.  The foreground context owns no Block, so the door refuses
// the wait and names the gate.
//
// Expected diagnostic: no candidate of wait, with a note on the gate of the
// context.

#include <fixy/os/Futex.h>
#include <foundation/effects/Ctx.h>

int main() {
    ::fixy::futex::FutexWord word{0};
    [[maybe_unused]] auto ended = ::fixy::futex::wait(::foundation::effects::testing::foreground(), word, 0);
    return 0;
}
