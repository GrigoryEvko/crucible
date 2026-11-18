// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A notify that finds a waiter wakes it with a futex call.  The foreground
// context owns no Block, so the event count refuses its notify and names the
// gate of the futex door.
//
// Expected diagnostic: no candidate of notify_one, with a note on the gate of
// the context.

#include <fixy/concurrent/EventCount.h>
#include <foundation/effects/Ctx.h>

int main() {
    ::fixy::concurrent::EventCount events{};
    events.notify_one(::foundation::effects::testing::foreground());
    return 0;
}
