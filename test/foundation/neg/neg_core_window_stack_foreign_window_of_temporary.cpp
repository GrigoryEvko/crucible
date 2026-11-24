// A foreign door of a temporary WindowStack.  The stack ends with the full
// expression, so the window that the door lends is the window of an object
// that no later code reads.  The door of a temporary stack is deleted.

#include <foundation/core/WindowStack.h>

int main() {
    return ::foundation::core::WindowStack<int, 8>{}.with_foreign_window(
        ::foundation::core::WindowCount{4}, [](auto window) noexcept { return static_cast<int>(window.size()); },
        [] noexcept { return 0; });
}
