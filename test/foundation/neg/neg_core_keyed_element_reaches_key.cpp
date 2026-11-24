// A body of the keyed element door of a FixedArray captures the key and
// lends the same element again.  The two bodies would hold two references to
// one element.  The gate of the door refuses a body that reaches a key of the
// brand of its owner.

#include <foundation/core/Array.h>

int main() {
    auto minted = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 4>);
    auto& array = minted.at(::foundation::core::index<0>());
    auto& key = minted.at(::foundation::core::index<1>());
    key = array.with_element(std::move(key), ::foundation::core::index<1>(), [&array, &key](int& outer) noexcept {
        array.with_element(key, ::foundation::core::index<1>(), [](int& inner) noexcept { inner = 2; });
        outer = 1;
    });
    return array.at(::foundation::core::index<1>());
}
