// A walk over branded views takes the views of one scope.  The walk compares
// no size, because the brand proves the sizes equal.  The outer view has four
// positions and the inner view has two, so a walk over the two would read
// past the inner view.  The bodies give their return types, so only the walk
// fails.

#include <foundation/core/Array.h>
#include <foundation/core/Strided.h>

#include <cstddef>

int main() {
    ::foundation::core::FixedArray<int, 4> values{1, 2, 3, 4};
    ::foundation::core::FixedArray<std::size_t, 1> const wide_sizes{4};
    ::foundation::core::FixedArray<std::size_t, 1> const narrow_sizes{2};
    ::foundation::core::FixedArray<std::size_t, 1> const strides{1};
    auto const wide =
        ::foundation::core::mint_strided_view(values.view(), wide_sizes.view(), strides.view(), 0).expect("it fits");
    auto const narrow =
        ::foundation::core::mint_strided_view(values.view(), narrow_sizes.view(), strides.view(), 0).expect("it fits");
    return ::foundation::core::mint_strided_scope(wide, [narrow](auto outer) noexcept -> int {
        return ::foundation::core::mint_strided_scope(narrow, [&outer](auto inner) noexcept -> int {
            int total = 0;
            ::foundation::core::walk([&total](int const& left, int const& right) noexcept { total += left + right; },
                                     outer, inner);
            return total;
        });
    });
}
