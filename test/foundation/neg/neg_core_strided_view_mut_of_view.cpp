// The door of a unique strided view takes the ViewMut of a frame.  A plain
// View has no brand and no frame, so it proves no unique writer, and the door
// refuses it.

#include <foundation/core/Array.h>
#include <foundation/core/Strided.h>

#include <cstddef>

int main() {
    auto [values, values_key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 6>, ::foundation::core::FixedArray<int, 6>{1, 2, 3, 4, 5, 6});
    ::foundation::core::FixedArray<std::size_t, 2> const sizes{2, 3};
    ::foundation::core::FixedArray<std::size_t, 2> const strides{3, 1};
    return values.with_mut(values_key, [&](auto run) noexcept -> int {
        auto const unique = ::foundation::core::mint_strided_view_mut(run.view(), sizes.view(), strides.view(), 0);
        return static_cast<int>(unique.is_some());
    });
}
