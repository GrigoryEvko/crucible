// The sizes of a view borrow the view, so the sizes of a temporary view
// would dangle at the end of the full expression.  The rvalue form is
// deleted.

#include <foundation/core/Array.h>
#include <foundation/core/Strided.h>

#include <cstddef>

int main() {
    auto [storage, storage_key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 6>, ::foundation::core::FixedArray<int, 6>{1, 2, 3, 4, 5, 6});
    ::foundation::core::FixedArray<std::size_t, 2> const sizes{2, 3};
    ::foundation::core::FixedArray<std::size_t, 2> const strides{3, 1};
    return storage.with_mut(storage_key, [&](auto values) noexcept -> int {
        auto const kept = ::foundation::core::mint_strided_view(values.view(), sizes.view(), strides.view(), 0)
                              .expect("it fits")
                              .sizes();
        return static_cast<int>(kept.size());
    });
}
