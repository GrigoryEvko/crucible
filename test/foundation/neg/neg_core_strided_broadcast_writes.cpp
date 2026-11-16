// A broadcast gives a view of const elements, because each index of an axis
// with a stride of zero reads one element.  A walk with a body that writes
// through the element of a broadcast is refused, so no write lands on one
// element many times.

#include <foundation/core/Array.h>
#include <foundation/core/Strided.h>

#include <cstddef>

int main() {
    ::foundation::core::FixedArray<int, 3> values{1, 2, 3};
    ::foundation::core::FixedArray<std::size_t, 1> const sizes{3};
    ::foundation::core::FixedArray<std::size_t, 1> const strides{1};
    ::foundation::core::FixedArray<std::size_t, 2> const wide{4, 3};
    auto const line =
        ::foundation::core::mint_strided_view(values.view(), sizes.view(), strides.view(), 0).expect("it fits");
    auto const rows = line.broadcast_to(wide.view()).expect("a row broadcasts to four rows");
    return static_cast<int>(::foundation::core::walk([](int& element) noexcept { element = 0; }, rows).is_ok());
}
