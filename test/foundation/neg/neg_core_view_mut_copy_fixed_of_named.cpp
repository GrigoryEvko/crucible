// A body copies a View of a fixed extent into a named ViewMut.  The copy of a
// fixed extent takes the ViewMut by value, as fill does, so a named ViewMut
// would need a copy, and a copy would be a second writer of the run.

#include <foundation/core/Array.h>

int main() {
    ::foundation::core::FixedArray<int, 4> source{1, 2, 3, 4};
    auto [target, target_key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 4>);
    target.with_mut(target_key, [&source](auto out) noexcept { ::foundation::core::copy(out, source.view()); });
    return 0;
}
