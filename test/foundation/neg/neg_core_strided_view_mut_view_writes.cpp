// The plain view of a unique strided view reads.  A plain view copies, so a
// plain view that writes would be a second writer of the elements of the
// unique view.  The view() of a unique view thus has const elements, and no
// plain view of int comes from it.

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
        ::foundation::core::StridedView<int, 2> const writer = unique.view();
        return writer.count();
    });
    return static_cast<int>(count);
}
