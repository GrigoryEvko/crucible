// A frame slot gives its body only to a caller that gives the slot up, so a
// take of a named slot, which would leave the moved body for a second read,
// does not compile.
#include <fixy/os/ThreadTasks.h>

namespace {
struct Body {
    void operator()() noexcept {}
};
}  // namespace

int main() {
    ::fixy::thread::FrameSlot<Body> slot{Body{}};
    slot.take()();
    return 0;
}
