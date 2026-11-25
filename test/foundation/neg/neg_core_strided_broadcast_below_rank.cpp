// A broadcast keeps each axis and adds leading axes, so a target of a fixed
// extent below the static rank of the view is refused at compile time.

#include <foundation/core/Array.h>
#include <foundation/core/Strided.h>

#include <cstddef>

int main() {
    auto [storage, storage_key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 6>, ::foundation::core::FixedArray<int, 6>{1, 2, 3, 4, 5, 6});
    ::foundation::core::FixedArray<std::size_t, 2> const sizes{2, 3};
    ::foundation::core::FixedArray<std::size_t, 2> const strides{3, 1};
    ::foundation::core::FixedArray<std::size_t, 1> const narrow{6};
    return storage.with_mut(storage_key, [&](auto values) noexcept -> int {
        auto const matrix =
            ::foundation::core::mint_strided_view(values.view(), sizes.view(), strides.view(), 0).expect("it fits");
        return static_cast<int>(matrix.broadcast_to(narrow.view()).is_some());
    });
}
