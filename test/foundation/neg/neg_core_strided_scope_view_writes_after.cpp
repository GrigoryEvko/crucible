// The scope of a unique strided view lends its branded view to the body for
// the call.  A plain view copies, so a plain view that writes would be a
// second writer, which a body can give to its caller through a value that
// holds no brand.  The plain view of a branded view reads, and the conversion
// of that read view to a plain view that writes does not compile.

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
    auto const is_written = values.with_mut(values_key, [&](auto run) noexcept -> bool {
        auto unique = ::foundation::core::mint_strided_view_mut(std::move(run), sizes.view(), strides.view(), 0)
                          .expect("a dense matrix has no overlap");
        int const scopes = ::foundation::core::mint_strided_scope(unique, [](auto branded) noexcept -> int {
            ::foundation::core::StridedView<int, 2> writer(branded.view());
            return ::foundation::core::walk([](int& element) noexcept { element = 0; }, writer).is_ok() ? 1 : 0;
        });
        return scopes == 1;
    });
    return static_cast<int>(is_written);
}
