// An index of a scope of a static rank takes one coordinate for each axis, so
// three coordinates for a view of rank 2 are refused at compile time.  The
// body gives its return type, so only the call of index() fails.

#include <foundation/core/Array.h>
#include <foundation/core/Strided.h>

#include <cstddef>

int main() {
    auto [storage, storage_key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 6>, ::foundation::core::FixedArray<int, 6>{1, 2, 3, 4, 5, 6});
    ::foundation::core::FixedArray<std::size_t, 2> const sizes{2, 3};
    ::foundation::core::FixedArray<std::size_t, 2> const strides{3, 1};
    return storage.with_mut(storage_key, [&](auto values) noexcept -> int {
        auto const matrix =
            ::foundation::core::mint_strided_view(values.view(), sizes.view(), strides.view(), 0).expect("it fits");
        return ::foundation::core::mint_strided_scope(matrix, [](auto view) noexcept -> int {
            return static_cast<int>(view.index(std::size_t{0}, std::size_t{1}, std::size_t{2}).is_some());
        });
    });
}
