// A thread-name witness claims a name for the thread that minted it.  A
// move would carry the claim to another thread, which carries no such name,
// so the witness is neither copyable nor movable: it stays in the frame of
// the named thread.
//
// The relocating function is never called.  The error is at the move, so it
// is reported whether or not a thread was ever named.

#include <fixy/os/ThreadName.h>

#include <utility>

namespace {
[[maybe_unused]] void relocate(fixy::ThreadNamed<"crux-a">& earned) {
    fixy::ThreadNamed<"crux-a"> elsewhere{std::move(earned)};
    (void)elsewhere;
}
}  // namespace

int main() { return 0; }
