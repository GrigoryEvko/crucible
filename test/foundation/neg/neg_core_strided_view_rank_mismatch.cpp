// Sizes and strides of two different fixed extents name no one rank, so the
// constraint of the door refuses them at compile time.

#include <foundation/core/Array.h>
#include <foundation/core/Strided.h>

#include <cstddef>

int main() {
    auto [values, values_key] = ::foundation::core::mint_keyed_owner(
        ::foundation::core::fixed_array_owner<int, 6>, ::foundation::core::FixedArray<int, 6>{1, 2, 3, 4, 5, 6});
    ::foundation::core::FixedArray<std::size_t, 2> const sizes{2, 3};
    ::foundation::core::FixedArray<std::size_t, 3> const strides{3, 1, 1};
    return values.with_mut(values_key, [&](auto run) noexcept -> int {
        return static_cast<int>(
            ::foundation::core::mint_strided_view(run.view(), sizes.view(), strides.view(), 0).is_some());
    });
}
