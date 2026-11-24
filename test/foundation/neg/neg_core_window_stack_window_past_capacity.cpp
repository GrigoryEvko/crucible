// A window door of a fixed extent asks a window of more elements than the
// stack holds.  Such a window never fits, so the count of the door is refused
// at compile time.

#include <foundation/core/WindowStack.h>

int main() {
    ::foundation::core::WindowStack<int, 8> stack{};
    return stack.with_window<9>([](auto window) noexcept { return static_cast<int>(window.size()); },
                                [] noexcept { return 0; });
}
