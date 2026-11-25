// update assigns its copy back, so the slot holds an element at each step.  An
// element with no assignment has no such step, so a list of it has no update.

#include <foundation/core/List.h>

namespace {
struct Fixed final {
    int value = 0;
    constexpr explicit Fixed(int from) noexcept : value{from} {}
    constexpr Fixed(Fixed const&) noexcept = default;
    constexpr Fixed(Fixed&&) noexcept = default;
    Fixed& operator=(Fixed const&) = delete("the fixture has no assignment");
    Fixed& operator=(Fixed&&) = delete("the fixture has no assignment");
};
}  // namespace

int main() {
    ::foundation::core::FixedList<Fixed, 2> list{};
    list.update(::foundation::core::index<0>(), [](Fixed& element) noexcept { element.value = 3; });
    return 0;
}
