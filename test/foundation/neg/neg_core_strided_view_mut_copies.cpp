// A unique strided view is the one writer of its elements, so it does not
// copy.  A copy would be a second writer of the same elements.

#include <foundation/core/Array.h>
#include <foundation/core/Strided.h>

#include <cstddef>
#include <utility>

int main() {
    auto [values, values_key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 6>, ::foundation::core::FixedArray<int, 6>{1, 2, 3, 4, 5, 6});
    ::foundation::core::FixedArray<std::size_t, 2> const sizes{2, 3};
    ::foundation::core::FixedArray<std::size_t, 2> const strides{3, 1};
    auto const count = values.with_mut(values_key, [&](auto run) noexcept {
        auto unique = ::foundation::core::mint_strided_view_mut(std::move(run), sizes.view(), strides.view(), 0)
                          .expect("a dense matrix has no overlap");
        auto second = unique;
        return second.count();
    });
    return static_cast<int>(count);
}
