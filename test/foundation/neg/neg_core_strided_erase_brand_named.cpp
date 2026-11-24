// erase_brand consumes the one writer of a unique strided view.  A named
// unique view stays with its holder, so the holder would keep a writer of the
// elements beside the erased view.  The door takes only an rvalue.

#include <foundation/core/Array.h>
#include <foundation/core/Strided.h>

#include <cstddef>
#include <utility>

int main() {
    ::foundation::core::FixedArray<int, 6> values{1, 2, 3, 4, 5, 6};
    ::foundation::core::FixedArray<std::size_t, 2> const sizes{2, 3};
    ::foundation::core::FixedArray<std::size_t, 2> const strides{3, 1};
    auto const count = values.with_mut([&](auto run) noexcept -> std::size_t {
        auto unique = ::foundation::core::mint_strided_view_mut(std::move(run), sizes.view(), strides.view(), 0)
                          .expect("a dense matrix has no overlap");
        return ::foundation::core::erase_brand(unique).count();
    });
    return static_cast<int>(count);
}
