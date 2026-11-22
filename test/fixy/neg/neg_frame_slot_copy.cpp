// A frame slot holds its body one time, so a copy of a slot does not compile.
#include <fixy/os/ThreadTasks.h>

namespace {
struct Body {
    void operator()() noexcept {}
};
}  // namespace

int main() {
    ::fixy::thread::FrameSlot<Body> slot{Body{}};
    ::fixy::thread::FrameSlot<Body> copy{slot};
    std::move(slot).take()();
    std::move(copy).take()();
    return 0;
}
