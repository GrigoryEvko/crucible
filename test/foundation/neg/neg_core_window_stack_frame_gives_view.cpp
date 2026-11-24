// The open arm of the window door of a WindowStack gives the View of its
// window out.  The door gives the window back when the arm returns, so the
// View would name the slots of the next window.  The result of a frame door
// holds no borrow, so the call binds to the result twin of the door.

#include <foundation/core/Region.h>
#include <foundation/core/WindowStack.h>

int main() {
    ::foundation::core::WindowStack<int, 8> stack{};
    auto const kept = stack.with_window(
        ::foundation::core::WindowCount{4}, [](auto window) noexcept { return window.view(); },
        [] noexcept { return ::foundation::core::View<int const>{}; });
    return static_cast<int>(kept.size());
}
