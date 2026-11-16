// An index of one strided scope is no index of another scope.  The outer
// view has four positions and the inner view has two, so the index 3 of the
// outer scope would read past the inner view, where at() has no check.  The
// bodies give their return types, so only the read of the inner view fails.

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
        auto const last = outer.index(std::size_t{3}).expect("3 is below 4");
        return ::foundation::core::mint_strided_scope(narrow,
                                                      [&last](auto inner) noexcept -> int { return inner.at(last); });
    });
}
