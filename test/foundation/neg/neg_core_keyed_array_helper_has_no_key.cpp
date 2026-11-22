// A helper that takes a keyed FixedArray, and no key, opens a frame of it.  A
// body of a frame of the array calls the helper, as the routes of a second
// frame through a callee do.  A keyed array opens a frame only with its key,
// and the helper has none.

#include <foundation/core/Array.h>

namespace {

template <class Array>
void open_without_key(Array& array) noexcept {
    array.with_mut([](auto inner) noexcept { static_cast<void>(inner.size()); });
}

}  // namespace

int main() {
    auto minted = ::foundation::core::mint_fixed_array<int, 4>();
    auto& array = minted.at(::foundation::core::index<0>());
    auto& key = minted.at(::foundation::core::index<1>());
    key = array.with_mut(std::move(key), [&array](auto outer) noexcept {
        open_without_key(array);
        static_cast<void>(outer.size());
    });
    return array.at(::foundation::core::index<0>());
}
