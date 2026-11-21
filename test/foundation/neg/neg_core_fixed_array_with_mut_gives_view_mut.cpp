// A body of the member frame door with_mut of a FixedArray gives its ViewMut
// out.  The ViewMut would then name the elements of the array after the
// frame, beside each later door of the array.  The result of a frame door
// holds no borrow.

#include <foundation/core/Array.h>

int main() {
    ::foundation::core::FixedArray<int, 4> array{};
    auto kept = array.with_mut([](auto run) noexcept { return run; });
    return static_cast<int>(kept.size());
}
