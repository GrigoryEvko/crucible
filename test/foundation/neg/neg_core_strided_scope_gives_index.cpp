// The body of a strided scope gives an index of the scope.  Outside the call
// the index would prove nothing about a later view of smaller sizes, and at()
// has no check, so the constraint of the mint refuses a result that names the
// brand.

#include <foundation/core/Array.h>
#include <foundation/core/Strided.h>

#include <cstddef>

int main() {
    ::foundation::core::FixedArray<int, 4> values{1, 2, 3, 4};
    ::foundation::core::FixedArray<std::size_t, 1> const sizes{4};
    ::foundation::core::FixedArray<std::size_t, 1> const strides{1};
    auto const line =
        ::foundation::core::mint_strided_view(values.view(), sizes.view(), strides.view(), 0).expect("it fits");
    auto const index =
        ::foundation::core::mint_strided_scope(line, [](auto view) noexcept { return view.index(std::size_t{3}); });
    return static_cast<int>(index.is_some());
}
