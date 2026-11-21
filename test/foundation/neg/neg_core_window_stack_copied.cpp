// A copy of a window stack.  Each open loan holds a Ref to its stack, so a
// copy would hold a top that no loan gives back.  A stack does not copy.

#include <foundation/core/WindowStack.h>

int main() {
    ::foundation::core::WindowStack<int, 4> const stack{};
    ::foundation::core::WindowStack<int, 4> const copy{stack};
    return static_cast<int>(copy.held());
}
