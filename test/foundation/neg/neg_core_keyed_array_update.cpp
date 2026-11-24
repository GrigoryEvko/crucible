// A keyed array has no update door: another name of the array could write an
// element while a frame of the array is open, and no key holds it back.

#include <foundation/core/Array.h>

int main() {
    auto [array, key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 4>);
    array.with_mut(key, [&written = array](auto run) noexcept {
        written.update(::foundation::core::index<0>(), [](int& element) noexcept { element = 2; });
        static_cast<void>(run.size());
    });
    return array.at(::foundation::core::index<0>());
}
