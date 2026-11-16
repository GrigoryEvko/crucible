// The views of one walk have one rank parameter.  A view of a static rank and
// a view of a dynamic rank give two ranks, so the walk is refused at compile
// time.  The caller converts the static view to the dynamic form first.

#include <foundation/core/Array.h>
#include <foundation/core/Strided.h>

#include <cstddef>

int main() {
    ::foundation::core::FixedArray<int, 6> values{1, 2, 3, 4, 5, 6};
    ::foundation::core::FixedArray<std::size_t, 2> const sizes{2, 3};
    ::foundation::core::FixedArray<std::size_t, 2> const strides{3, 1};
    ::foundation::core::View<std::size_t const> const dynamic_sizes = sizes.view();
    ::foundation::core::View<std::size_t const> const dynamic_strides = strides.view();
    auto const fixed =
        ::foundation::core::mint_strided_view(values.view(), sizes.view(), strides.view(), 0).expect("it fits");
    auto const dynamic =
        ::foundation::core::mint_strided_view(values.view(), dynamic_sizes, dynamic_strides, 0).expect("it fits");
    return static_cast<int>(
        ::foundation::core::walk([](int& left, int& right) noexcept { left += right; }, fixed, dynamic).is_ok());
}
