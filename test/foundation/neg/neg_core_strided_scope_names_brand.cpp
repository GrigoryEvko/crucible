// The pack of the scope mint refuses each explicit template argument, so no
// caller names a brand that it already holds and opens a second scope of it.

#include <foundation/core/Array.h>
#include <foundation/core/Strided.h>

#include <cstddef>

int main() {
    auto [storage, storage_key] = ::foundation::core::mint_keyed_owner(
        ::foundation::core::fixed_array_owner<int, 4>, ::foundation::core::FixedArray<int, 4>{1, 2, 3, 4});
    ::foundation::core::FixedArray<std::size_t, 1> const sizes{4};
    ::foundation::core::FixedArray<std::size_t, 1> const strides{1};
    return storage.with_mut(storage_key, [&](auto values) noexcept -> int {
        auto const line =
            ::foundation::core::mint_strided_view(values.view(), sizes.view(), strides.view(), 0).expect("it fits");
        return static_cast<int>(
            ::foundation::core::mint_strided_scope<int>(line, [](auto view) noexcept { return view.rank(); }));
    });
}
