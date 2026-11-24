// An assignment of a keyed array writes its elements with no key.  A body of a
// frame of the array can reach the array by a name, so a helper that the body
// calls could assign it while the ViewMut of the frame lives.

#include <foundation/core/Array.h>

namespace {

template <class Array>
void clear(Array& array, Array const& zeros) noexcept {
    array = zeros;
}

}  // namespace

int main() {
    auto [array, key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 4>);
    auto const zeros = array;
    array.with_mut(key, [&written = array, &zeros](auto run) noexcept {
        clear(written, zeros);
        static_cast<void>(run.size());
    });
    return array.at(::foundation::core::index<0>());
}
