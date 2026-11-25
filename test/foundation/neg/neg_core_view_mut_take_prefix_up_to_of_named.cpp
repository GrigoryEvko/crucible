// A body takes the prefix of a named ViewMut.  The name stays with the body,
// so the prefix would be a second writer of the run.  The prefix takes the
// ViewMut by move.

#include <foundation/core/Array.h>

int main() {
    auto [array, key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 4>, ::foundation::core::FixedArray<int, 4>{1, 2, 3, 4});
    array.with_mut(key, [](auto out) noexcept {
        auto part = ::foundation::core::take_prefix_up_to(out, 2);
        static_cast<void>(part);
    });
    return 0;
}
