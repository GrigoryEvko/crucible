// A const reference to a unique strided view reads its elements, and it
// writes none of them.  The scope of a unique view gives a branded view that
// writes, so the scope refuses a const unique view.

#include <foundation/core/Array.h>
#include <foundation/core/Strided.h>

#include <cstddef>
#include <utility>

int main() {
    ::foundation::core::FixedArray<int, 6> values{1, 2, 3, 4, 5, 6};
    ::foundation::core::FixedArray<std::size_t, 2> const sizes{2, 3};
    ::foundation::core::FixedArray<std::size_t, 2> const strides{3, 1};
    auto const rank = values.with_mut([&](auto run) noexcept -> std::size_t {
        auto const unique = ::foundation::core::mint_strided_view_mut(std::move(run), sizes.view(), strides.view(), 0)
                                .expect("a dense matrix has no overlap");
        return ::foundation::core::mint_strided_scope(unique, [](auto branded) noexcept { return branded.rank(); });
    });
    return static_cast<int>(rank);
}
