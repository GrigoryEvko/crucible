// The compile-time checks of foundation/core/Strided.h.

#include <foundation/core/Strided.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace foundation::core {

namespace detail::strided_checks {

// A brand that the checks name.  A program gets each brand from the mint.
struct CheckBrand final {};
struct OtherBrand final {};

// A view of a static rank holds its pointer and two lanes for each axis.  A
// view of a dynamic rank holds the lanes of max_strided_rank axes and the
// rank.
static_assert(sizeof(StridedView<int, 2>) == 5 * sizeof(std::size_t));
static_assert(sizeof(StridedView<int>) == (2 + 2 * max_strided_rank) * sizeof(std::size_t));
// The seal refuses std::bit_cast, and the copy stays trivial, so the ABI
// passes a small view in registers.
static_assert(!std::is_trivially_copyable_v<StridedView<float, 3>>);
static_assert(std::is_trivially_copy_constructible_v<StridedView<float, 3>>);
static_assert(std::is_trivially_destructible_v<StridedView<float>>);

// No pointer becomes a view, and a view converts only to a view of a const
// element or of a dynamic rank.
static_assert(!std::is_constructible_v<StridedView<int, 1>, int*>);
static_assert(!std::is_default_constructible_v<StridedView<int, 1>>);
static_assert(std::is_convertible_v<StridedView<int, 2>, StridedView<int const, 2>>);
static_assert(std::is_convertible_v<StridedView<int, 2>, StridedView<int const>>);
static_assert(!std::is_convertible_v<StridedView<int const, 2>, StridedView<int, 2>>);
static_assert(!std::is_convertible_v<StridedView<int>, StridedView<int, 2>>);
static_assert(!std::is_convertible_v<StridedView<int, 2>, StridedView<int, 3>>);

// An index of one brand is no index of another brand, and no number becomes
// an index.
static_assert(!std::is_convertible_v<StridedIndex<2, CheckBrand>, StridedIndex<2, OtherBrand>>);
static_assert(!std::is_constructible_v<StridedIndex<2, CheckBrand>, std::size_t, std::size_t>);
static_assert(!std::is_constructible_v<BrandedStridedView<int, 2, CheckBrand>, StridedView<int, 2>>);

// The body of a scope gives a value that does not hold the brand.
struct GivesCount final {
    template <class Brand>
    constexpr std::size_t operator()(BrandedStridedView<int, 2, Brand> view) const noexcept {
        return view.rank();
    }
};
struct GivesIndex final {
    template <class Brand>
    constexpr Option<StridedIndex<2, Brand>> operator()(BrandedStridedView<int, 2, Brand> view) const noexcept {
        return view.index(std::size_t{0}, std::size_t{0});
    }
};
static_assert(CanMintStridedScope<int, 2, GivesCount, CheckBrand>);
static_assert(!CanMintStridedScope<int, 2, GivesIndex, CheckBrand>);

// A unique view holds the parts of a view and nothing more.  It moves and
// does not copy, and no plain view becomes a unique view.
static_assert(sizeof(StridedViewMut<int, 2, CheckBrand>) == sizeof(StridedView<int, 2>));
static_assert(!std::is_trivially_copyable_v<StridedViewMut<int, 2, CheckBrand>>);
static_assert(!std::is_copy_constructible_v<StridedViewMut<int, 2, CheckBrand>>);
static_assert(!std::is_copy_assignable_v<StridedViewMut<int, 2, CheckBrand>>);
static_assert(std::is_nothrow_move_constructible_v<StridedViewMut<int, 2, CheckBrand>>);
static_assert(!std::is_constructible_v<StridedViewMut<int, 2, CheckBrand>, StridedView<int, 2>>);
static_assert(!std::is_default_constructible_v<StridedViewMut<int, 2, CheckBrand>>);

// The test of no overlap: a dense matrix and its transpose pass, a stride
// that steps inside the span of a smaller axis and a stride of zero fail,
// and an axis of size one reads no stride.
[[nodiscard]] consteval bool has_no_overlap(std::size_t rows, std::size_t columns, std::size_t row_stride,
                                            std::size_t column_stride) noexcept {
    StridedLanes<2> lanes{};
    lanes.sizes[0] = rows;
    lanes.sizes[1] = columns;
    lanes.strides[0] = row_stride;
    lanes.strides[1] = column_stride;
    return has_no_overlap_(lanes);
}
static_assert(has_no_overlap(3, 4, 4, 1));
static_assert(has_no_overlap(3, 4, 1, 3));
static_assert(has_no_overlap(3, 4, 5, 1));
static_assert(!has_no_overlap(3, 4, 3, 1));
static_assert(!has_no_overlap(3, 4, 0, 1));
static_assert(has_no_overlap(1, 4, 0, 1));
static_assert(!has_no_overlap(3, 2, 2, 3));

// The view of a 3 x 4 matrix in row-major order over 12 elements.
[[nodiscard]] consteval int matrix_sum() noexcept {
    FixedArray<int, 12> values{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    FixedArray<std::size_t, 2> const sizes{3, 4};
    FixedArray<std::size_t, 2> const strides{4, 1};
    StridedView<int, 2> const matrix =
        mint_strided_view(values.view(), sizes.view(), strides.view(), 0).expect("the matrix fits");
    int total = 0;
    if (walk([&total](int& element) noexcept { total += element; }, matrix).is_err()) return -1;
    return total;
}
static_assert(matrix_sum() == 66);

// The walk over the branded view of a scope of the same matrix.
[[nodiscard]] consteval int scope_sum() noexcept {
    FixedArray<int, 12> values{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    FixedArray<std::size_t, 2> const sizes{3, 4};
    FixedArray<std::size_t, 2> const strides{4, 1};
    StridedView<int, 2> const matrix =
        mint_strided_view(values.view(), sizes.view(), strides.view(), 0).expect("the matrix fits");
    return mint_strided_scope(matrix, [](auto branded) noexcept {
        int total = 0;
        walk([&total](int& element) noexcept { total += element; }, branded);
        return total;
    });
}
static_assert(scope_sum() == 66);

// A strided view refers to its elements (THE TRANSFER).  The walk above
// writes each element through a const view of int, so a const view of
// elements that are not const is no shared read.  A view of const elements
// moves to another thread and two threads read it.
static_assert(::foundation::transfer::Sendable<StridedView<float const, 2>>
              && ::foundation::transfer::Shareable<StridedView<float const, 2>>);
static_assert(::foundation::transfer::Sendable<StridedView<float const, 2> const&>);
static_assert(!::foundation::transfer::Shareable<StridedView<float, 2>>
              && !::foundation::transfer::Sendable<StridedView<float, 2> const&>);

}  // namespace detail::strided_checks

}  // namespace foundation::core
