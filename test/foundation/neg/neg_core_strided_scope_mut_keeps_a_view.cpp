// The scope of a unique strided view lends its branded view to the body for
// the call.  A body that captures an Option of a view by reference could keep
// the view in it, and the caller could read the view after the scope, while
// the unique view writes again.  The scope refuses such a body, and its store
// twin gives the reason.

#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Strided.h>

#include <cstddef>
#include <utility>

int main() {
    auto [values, values_key] = ::foundation::core::mint_keyed_owner(
        ::foundation::core::fixed_array_owner<int, 6>, ::foundation::core::FixedArray<int, 6>{1, 2, 3, 4, 5, 6});
    ::foundation::core::FixedArray<std::size_t, 2> const sizes{2, 3};
    ::foundation::core::FixedArray<std::size_t, 2> const strides{3, 1};
    auto const is_kept = values.with_mut(values_key, [&](auto run) noexcept -> bool {
        auto unique = ::foundation::core::mint_strided_view_mut(std::move(run), sizes.view(), strides.view(), 0)
                          .expect("a dense matrix has no overlap");
        ::foundation::core::Option<::foundation::core::StridedView<int const, 2>> kept{::foundation::core::none};
        int const scopes = ::foundation::core::mint_strided_scope(unique, [&kept](auto branded) noexcept -> int {
            kept.fill(branded.view());
            return 1;
        });
        return scopes == 1 && kept.is_some();
    });
    return static_cast<int>(is_kept);
}
