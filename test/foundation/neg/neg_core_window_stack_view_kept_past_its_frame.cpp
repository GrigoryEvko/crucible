// The open arm of the window door of a WindowStack keeps the View of its
// ViewMut in a variable that it captures by reference.  The View outlives the
// window, so a write of the next window of the same slots would show through
// it, and a read after the end of the stack would read a dead stack frame.
// The store twin of the door binds to the arm, so the refusal gives the
// reason of the store check.

#include <foundation/core/Choice.h>
#include <foundation/core/Region.h>
#include <foundation/core/WindowStack.h>

int main() {
    ::foundation::core::Option<::foundation::core::View<int const>> kept{::foundation::core::none};
    ::foundation::core::WindowStack<int, 8> stack{};
    stack.with_window(
        ::foundation::core::WindowCount{4},
        [&kept](auto window) noexcept {
            kept = ::foundation::core::Option<::foundation::core::View<int const>>::some(window.view());
        },
        [] noexcept {});
    return kept.is_some() ? 0 : 1;
}
