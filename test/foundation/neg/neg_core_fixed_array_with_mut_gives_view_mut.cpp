// A body of the member frame door with_mut of a keyed FixedArray gives its ViewMut
// out.  The ViewMut would then name the elements of the array after the
// frame, beside each later door of the array.  The result of a frame door
// holds no borrow.

#include <foundation/core/Array.h>

int main() {
    auto [array, key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 4>);
    auto kept = array.with_mut(key, [](auto run) noexcept { return run; });
    return static_cast<int>(kept.size());
}
