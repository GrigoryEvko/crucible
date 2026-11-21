// A view of a dynamic rank does not convert to a view of a static rank,
// because its rank is a value that no type proves.  ranked<R>() is the
// checked road, and it gives an Option.

#include <foundation/core/Array.h>
#include <foundation/core/Strided.h>

#include <cstddef>

int main() {
    ::foundation::core::FixedArray<int, 6> storage{1, 2, 3, 4, 5, 6};
    ::foundation::core::FixedArray<std::size_t, 2> const sizes{2, 3};
    ::foundation::core::FixedArray<std::size_t, 2> const strides{3, 1};
    ::foundation::core::View<std::size_t const> const dynamic_sizes = sizes.view();
    ::foundation::core::View<std::size_t const> const dynamic_strides = strides.view();
    return storage.with_mut([&](auto values) noexcept -> int {
        ::foundation::core::StridedView<int> const dynamic =
            ::foundation::core::mint_strided_view(values.view(), dynamic_sizes, dynamic_strides, 0).expect("it fits");
        ::foundation::core::StridedView<int, 2> const fixed = dynamic;
        return static_cast<int>(fixed.count());
    });
}
