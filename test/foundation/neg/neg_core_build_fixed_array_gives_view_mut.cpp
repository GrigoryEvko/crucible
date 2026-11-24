// A body of build_fixed_array gives its ViewMut out.  The ViewMut would name
// the elements of a local of the door after the door ends.  The result of a
// frame door holds no borrow.

#include <foundation/core/Array.h>

int main() {
    auto kept = ::foundation::core::build_fixed_array<int, 4>([](auto run) noexcept { return run; });
    return static_cast<int>(kept.size());
}
