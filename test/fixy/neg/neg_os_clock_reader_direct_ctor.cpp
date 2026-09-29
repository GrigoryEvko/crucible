// The reader's one door is the mint, whose requires-clause is the gate
// that keeps a clock read off the replay-bound foreground path.  A reader
// built anywhere else is one built without that evidence, so the
// constructor is private and the mint is its only friend.
//
// MonotonicClock is an alias for the reader over the monotonic source,
// so the private constructor it names is the same door.

#include <fixy/os/Time.h>

int main() {
    fixy::time::MonotonicClock forged{};
    (void)forged;
    return 0;
}
