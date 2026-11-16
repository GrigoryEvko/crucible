// Sizes and strides of two different fixed extents name no one rank, so the
// constraint of the door refuses them at compile time.

#include <foundation/core/Array.h>
#include <foundation/core/Strided.h>

#include <cstddef>

int main() {
    ::foundation::core::FixedArray<int, 6> values{1, 2, 3, 4, 5, 6};
    ::foundation::core::FixedArray<std::size_t, 2> const sizes{2, 3};
    ::foundation::core::FixedArray<std::size_t, 3> const strides{3, 1, 1};
    return static_cast<int>(
        ::foundation::core::mint_strided_view(values.view(), sizes.view(), strides.view(), 0).is_some());
}
