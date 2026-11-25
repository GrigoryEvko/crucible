// A body of a frame door splits a const ViewMut into chunks.  A const ViewMut
// writes nothing, so it gives no writer of a part, and the split refuses it.

#include <foundation/core/Array.h>

#include <utility>

int main() {
    auto [array, key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 4>,
                                                             ::foundation::core::FixedArray<int, 4>{1, 2, 3, 4});
    array.with_mut(key, [](auto out) noexcept {
        auto const& shared = out;
        auto chunks = ::foundation::core::split_chunks(shared, 2);
        static_cast<void>(chunks);
    });
    return 0;
}
