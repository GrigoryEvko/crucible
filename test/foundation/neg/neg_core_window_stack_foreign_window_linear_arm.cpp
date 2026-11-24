// The open arm of a foreign door of a WindowStack captures by value a value
// that is linear.  The door calls the arm with no consume, so the linear
// value would end with the arm, where no rule reads it.  A foreign door
// checks no result and no capture, and it still refuses such an arm.

#include <foundation/Linear.h>
#include <foundation/core/WindowStack.h>

#include <utility>

namespace {
struct[[= ::foundation::linear::linear{}]][[= ::foundation::lifetime::no_start_over_bytes{}]] End {
    int handle = 0;
    constexpr explicit End(int value) noexcept : handle{value} {}
    constexpr End(End&&) noexcept = default;
    constexpr End& operator=(End&& other) noexcept {
        handle = other.handle;
        return *this;
    }
    End(End const&) = delete("a linear end of the fixture does not copy");
    End& operator=(End const&) = delete("a linear end of the fixture does not copy");
};
}  // namespace

int main() {
    ::foundation::core::WindowStack<int, 8> stack{};
    End end{3};
    return stack.with_foreign_window<4>(
        [held = std::move(end)](auto window) noexcept { return static_cast<int>(window.size()) + held.handle; },
        [] noexcept { return 0; });
}
