// The door of a strided view takes sizes of std::size_t only.  A size of int
// can be negative, so the constraint of the mint refuses it, and no negative
// size becomes the count of an axis.

#include <foundation/core/Array.h>
#include <foundation/core/Strided.h>

#include <cstddef>

int main() {
    ::foundation::core::FixedArray<int, 4> values{1, 2, 3, 4};
    ::foundation::core::FixedArray<int, 1> const sizes{4};
    ::foundation::core::FixedArray<std::size_t, 1> const strides{1};
    return static_cast<int>(
        ::foundation::core::mint_strided_view(values.view(), sizes.view(), strides.view(), 0).is_some());
}
