// A body of the keyed element door of a FixedArray captures the key and
// lends the same element again.  The two bodies would hold two references to
// one element.  The gate of the door refuses a body that reaches a key of the
// brand of its owner.

#include <foundation/core/Array.h>

int main() {
    auto [array, key] = ::foundation::core::mint_fixed_array<int, 4>();
    key = array.with_element(std::move(key), ::foundation::core::index<1>(), [&array, &key](int& outer) noexcept {
        array.with_element(key, ::foundation::core::index<1>(), [](int& inner) noexcept { inner = 2; });
        outer = 1;
    });
    return array.at(::foundation::core::index<1>());
}
