// A body of the keyed frame door of a FixedArray captures the moved key and
// the array, and opens a second frame of the array with it.  The two bodies
// would hold two ViewMuts of two brands over one run.  The gate of the door
// refuses a body that reaches a key of the brand of its owner.

#include <foundation/core/Array.h>

int main() {
    auto [array, key] = ::foundation::core::mint_fixed_array<int, 4>();
    key = array.with_mut(std::move(key), [&array, &key](auto outer) noexcept {
        key = array.with_mut(std::move(key), [](auto inner) noexcept { static_cast<void>(inner.size()); });
        static_cast<void>(outer.size());
    });
    return array.at(::foundation::core::index<0>());
}
