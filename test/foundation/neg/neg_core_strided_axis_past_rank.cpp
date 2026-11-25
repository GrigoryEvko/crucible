// An axis of a view of a static rank is an Index of the rank, so the constant
// axis 2 of a view of rank 2 does not convert, and no transpose reads a lane
// past the rank.

#include <foundation/core/Array.h>
#include <foundation/core/Strided.h>

#include <cstddef>

int main() {
    auto [storage, storage_key] = ::foundation::core::mint_keyed_owner(
        ::foundation::core::fixed_array_owner<int, 6>, ::foundation::core::FixedArray<int, 6>{1, 2, 3, 4, 5, 6});
    ::foundation::core::FixedArray<std::size_t, 2> const sizes{2, 3};
    ::foundation::core::FixedArray<std::size_t, 2> const strides{3, 1};
    return storage.with_mut(storage_key, [&](auto values) noexcept -> int {
        auto const matrix =
            ::foundation::core::mint_strided_view(values.view(), sizes.view(), strides.view(), 0).expect("it fits");
        auto const turned = matrix.transpose(::foundation::core::index<0>(), ::foundation::core::index<2>());
        return static_cast<int>(turned.count());
    });
}
