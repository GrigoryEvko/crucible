// The reader's one door is the mint, whose requires-clause is the gate
// that keeps a clock read off the replay-bound foreground path.  A reader
// built anywhere else is one built without that evidence, so the
// constructor is private and the mint is its only friend.
//
// The ported name is used on purpose: MonotonicClock names the clamped,
// gated reader, and the door it lacks is the same one.

#include <fixy/os/Time.h>

int main() {
    fixy::time::MonotonicClock forged{};
    (void)forged;
    return 0;
}
