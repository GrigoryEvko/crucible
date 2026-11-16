// The door of a unique strided view takes the ViewMut of a frame.  A plain
// View has no brand and no frame, so it proves no unique writer, and the door
// refuses it.

#include <foundation/core/Array.h>
#include <foundation/core/Strided.h>

#include <cstddef>

int main() {
    ::foundation::core::FixedArray<int, 6> values{1, 2, 3, 4, 5, 6};
    ::foundation::core::FixedArray<std::size_t, 2> const sizes{2, 3};
    ::foundation::core::FixedArray<std::size_t, 2> const strides{3, 1};
    auto const unique = ::foundation::core::mint_strided_view_mut(values.view(), sizes.view(), strides.view(), 0);
    return static_cast<int>(unique.is_some());
}
