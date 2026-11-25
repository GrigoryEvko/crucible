// A body of a frame door copies its ViewMut.  The copy would be a second
// writer of the run beside the first one, so a ViewMut has no copy, and one
// body holds one unique borrow of its frame.

#include <foundation/core/Array.h>

#include <utility>

int main() {
    auto [array, key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 4>, ::foundation::core::FixedArray<int, 4>{1, 2, 3, 4});
    array.with_mut(key, [](auto out) noexcept {
        auto second = out;
        static_cast<void>(second.size());
    });
    return 0;
}
