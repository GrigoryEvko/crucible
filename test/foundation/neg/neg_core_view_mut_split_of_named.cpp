// A body of a frame door splits a named ViewMut.  The name stays with the
// body, so the parts would be second writers of the run beside it.  A split
// takes its ViewMut by move.

#include <foundation/core/Array.h>

#include <utility>

int main() {
    auto [array, key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 4>,
                                                             ::foundation::core::FixedArray<int, 4>{1, 2, 3, 4});
    array.with_mut(key, [](auto out) noexcept {
        auto parts = ::foundation::core::split_at(out, 2);
        static_cast<void>(parts);
    });
    return 0;
}
