// A body gives the parts of a const ViewMut to a body.  A const ViewMut
// writes nothing, so it gives no writer of a part.

#include <foundation/core/Array.h>

int main() {
    auto [array, key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 8>);
    array.with_mut(key, [](auto out) noexcept {
        auto const& shared = out;
        auto rest = ::foundation::core::for_each_chunk<4>(shared, [](auto part) noexcept { static_cast<void>(part); });
        static_cast<void>(rest);
    });
    return 0;
}
