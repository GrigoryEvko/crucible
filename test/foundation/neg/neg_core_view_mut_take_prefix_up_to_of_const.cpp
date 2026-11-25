// A body takes the prefix of a const ViewMut.  A const ViewMut writes
// nothing, so it gives no writer of a prefix.

#include <foundation/core/Array.h>

int main() {
    auto [array, key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 4>, ::foundation::core::FixedArray<int, 4>{1, 2, 3, 4});
    array.with_mut(key, [](auto out) noexcept {
        auto const& shared = out;
        auto part = ::foundation::core::take_prefix_up_to(shared, 2);
        static_cast<void>(part);
    });
    return 0;
}
