// A body of the keyed frame door of a FixedArray calls a closure of its own
// text, and the closure opens a second frame of the array with the key.  The
// closure takes the key from the body, so the body captures the key, and the
// gate of the door refuses it.

#include <foundation/core/Array.h>

int main() {
    auto [array, key] = ::foundation::core::mint_fixed_array<int, 4>();
    key = array.with_mut(std::move(key), [&array, &key](auto outer) noexcept {
        [&array, &key]() noexcept {
            array.with_mut(key, [](auto inner) noexcept { static_cast<void>(inner.size()); });
        }();
        static_cast<void>(outer.size());
    });
    return array.at(::foundation::core::index<0>());
}
