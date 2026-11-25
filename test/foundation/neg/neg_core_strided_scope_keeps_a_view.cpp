// The scope of a strided view lends its branded view to the body for the call.
// A body that captures an Option of a view by reference could keep the view in
// it, and the caller could read the view after the scope.  The scope refuses
// such a body, and its store twin gives the reason.

#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
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
        ::foundation::core::Option<::foundation::core::StridedView<int const, 1>> kept{::foundation::core::none};
        int const scopes = ::foundation::core::mint_strided_scope(line, [&kept](auto branded) noexcept -> int {
            kept.fill(branded.view());
            return 1;
        });
        return scopes + static_cast<int>(kept.is_some());
    });
}
