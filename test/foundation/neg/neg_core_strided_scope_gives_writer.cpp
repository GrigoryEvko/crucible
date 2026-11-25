// The body of the scope of a unique strided view gives the plain view of its
// branded view.  That plain view copies, so it would read the elements after
// the borrow of the call.  The scope of a unique view takes only a body whose
// result holds no borrow.

#include <foundation/core/Array.h>
#include <foundation/core/Strided.h>

#include <cstddef>
#include <utility>

int main() {
    auto [values, values_key] = ::foundation::core::mint_keyed_owner(
        ::foundation::core::fixed_array_owner<int, 6>, ::foundation::core::FixedArray<int, 6>{1, 2, 3, 4, 5, 6});
    ::foundation::core::FixedArray<std::size_t, 2> const sizes{2, 3};
    ::foundation::core::FixedArray<std::size_t, 2> const strides{3, 1};
    auto const count = values.with_mut(values_key, [&](auto run) noexcept -> std::size_t {
        auto unique = ::foundation::core::mint_strided_view_mut(std::move(run), sizes.view(), strides.view(), 0)
                          .expect("a dense matrix has no overlap");
        auto const leaked =
            ::foundation::core::mint_strided_scope(unique, [](auto branded) noexcept { return branded.view(); });
        return leaked.count();
    });
    return static_cast<int>(count);
}
