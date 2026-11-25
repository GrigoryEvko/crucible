// The scope of a unique strided view lends its branded view to the body for
// the call.  A plain view copies, so a plain view that writes, which the body
// keeps through a reference capture, would be a second writer after the
// scope.  The plain view of a branded view reads, and the conversion of that
// read view to a plain view that writes does not compile.

#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Strided.h>

#include <cstddef>
#include <utility>

int main() {
    auto [values, values_key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 6>, ::foundation::core::FixedArray<int, 6>{1, 2, 3, 4, 5, 6});
    ::foundation::core::FixedArray<std::size_t, 2> const sizes{2, 3};
    ::foundation::core::FixedArray<std::size_t, 2> const strides{3, 1};
    auto const is_written = values.with_mut(values_key, [&](auto run) noexcept -> bool {
        auto unique = ::foundation::core::mint_strided_view_mut(std::move(run), sizes.view(), strides.view(), 0)
                          .expect("a dense matrix has no overlap");
        ::foundation::core::Option<::foundation::core::StridedView<int, 2>> kept{::foundation::core::none};
        int const scopes = ::foundation::core::mint_strided_scope(unique, [&kept](auto branded) noexcept -> int {
            kept.fill(branded.view());
            return 1;
        });
        bool const is_zeroed = std::move(kept).match(
            [](::foundation::core::StridedView<int, 2> writer) noexcept {
                return ::foundation::core::walk([](int& element) noexcept { element = 0; }, writer).is_ok();
            },
            [] noexcept { return false; });
        return is_zeroed && scopes == 1;
    });
    return static_cast<int>(is_written);
}
