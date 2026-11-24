// A body of the keyed frame door of a FixedArray opens a second frame through
// a Ref of the array.  A keyed array opens a frame only with its key, and the
// Ref carries none.

#include <foundation/core/Array.h>
#include <foundation/core/Ref.h>

int main() {
    auto [array, key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 4>);
    auto const alias = ::foundation::core::to(array);
    key = array.with_mut(std::move(key), [&alias](auto outer) noexcept {
        (*alias).with_mut([](auto inner) noexcept { static_cast<void>(inner.size()); });
        static_cast<void>(outer.size());
    });
    return array.at(::foundation::core::index<0>());
}
