// A body takes a window of a fixed extent of a const ViewMut.  A const
// ViewMut writes nothing, so it gives no writer of a window.

#include <foundation/core/Array.h>

int main() {
    auto [array, key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 4>, ::foundation::core::FixedArray<int, 4>{1, 2, 3, 4});
    array.with_mut(key, [](auto out) noexcept {
        auto const& shared = out;
        auto part = ::foundation::core::take_window<2>(shared, 1);
        static_cast<void>(part);
    });
    return 0;
}
