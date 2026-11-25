// A body fills a named ViewMut.  The fill of a ViewMut takes it by value, as
// copy and write_each do, so a named ViewMut would need a copy, and a copy
// would be a second writer of the run.

#include <foundation/core/Array.h>

int main() {
    auto [array, key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 4>, ::foundation::core::FixedArray<int, 4>{1, 2, 3, 4});
    array.with_mut(key, [](auto out) noexcept { ::foundation::core::fill(out, 0); });
    return 0;
}
