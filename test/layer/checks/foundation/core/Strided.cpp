// The compile-time checks of foundation/core/Strided.h.

#include <foundation/core/Strided.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

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

// The body of a scope holds no linear value by value: the mint calls it, and
// a call is no consume.
struct[[= ::foundation::linear::linear{}]][[= ::foundation::lifetime::no_start_over_bytes{}]] ScopeToken {
    int handle = 0;
    constexpr explicit ScopeToken(int value) noexcept : handle{value} {}
    constexpr ScopeToken(ScopeToken&&) noexcept = default;
    constexpr ScopeToken& operator=(ScopeToken&& other) noexcept {
        handle = other.handle;
        return *this;
    }
    ScopeToken(ScopeToken const&) = delete("a linear token of the check does not copy");
};
struct GivesCountWithToken final {
    ScopeToken held;
    template <class Brand>
    constexpr std::size_t operator()(BrandedStridedView<int, 2, Brand> view) const noexcept {
        return view.rank();
    }
};
static_assert(!CanMintStridedScope<int, 2, GivesCountWithToken, CheckBrand>);

// A unique view holds the parts of a view and nothing more.  It moves and
// does not copy, and no plain view becomes a unique view.  Its moves empty the
// source, so no move is trivial.
static_assert(sizeof(StridedViewMut<int, 2, CheckBrand>) == sizeof(StridedView<int, 2>));
static_assert(!std::is_trivially_copyable_v<StridedViewMut<int, 2, CheckBrand>>);
static_assert(!std::is_copy_constructible_v<StridedViewMut<int, 2, CheckBrand>>);
static_assert(!std::is_copy_assignable_v<StridedViewMut<int, 2, CheckBrand>>);
static_assert(std::is_nothrow_move_constructible_v<StridedViewMut<int, 2, CheckBrand>>);
static_assert(!std::is_trivially_move_constructible_v<StridedViewMut<int, 2, CheckBrand>>
              && !std::is_trivially_move_assignable_v<StridedViewMut<int, 2, CheckBrand>>);
static_assert(!std::is_constructible_v<StridedViewMut<int, 2, CheckBrand>, StridedView<int, 2>>);
static_assert(!std::is_default_constructible_v<StridedViewMut<int, 2, CheckBrand>>);

// A View has no extent 0, so the mint gives no unique view of the static rank
// 0.  Such a view would keep its count of 1 after a move, so no count would
// refuse a read of the moved view.  A door that gives a unique view of rank 0
// needs the test of require_run_ (foundation/core/Region.h) on the first
// element, in view() and in detail::writable_view_of_ of a StridedViewMut.
template <std::size_t Extent>
concept ExtentOfAView = requires { typename View<std::size_t const, Extent>; };
static_assert(ExtentOfAView<1> && !ExtentOfAView<0>);

// The plain view of a unique view reads: its elements are const, also for a
// unique view that is not const.  A copy of a plain view that writes would be
// a second writer.
static_assert(
    std::is_same_v<decltype(std::declval<StridedViewMut<int, 2, CheckBrand>&>().view()), StridedView<int const, 2>>);

// The plain view of a branded view reads: its elements are const, also for
// a branded view whose elements are not const.  A copy of a plain view that
// writes would be a second writer after the scope.
static_assert(std::is_same_v<decltype(std::declval<BrandedStridedView<int, 2, CheckBrand> const&>().view()),
                             StridedView<int const, 2>>);

// The body of the scope of a unique view gives a value that holds no borrow,
// so the result holds no plain view of the branded view, which reads the
// elements after the borrow.  The scope of a plain view takes that body.
struct GivesView final {
    template <class Brand>
    constexpr StridedView<int const, 2> operator()(BrandedStridedView<int, 2, Brand> view) const noexcept {
        return view.view();
    }
};
static_assert(CanMintUniqueStridedScope<int, 2, GivesCount, CheckBrand>);
static_assert(CanMintStridedScope<int, 2, GivesView, CheckBrand>);
static_assert(!CanMintUniqueStridedScope<int, 2, GivesView, CheckBrand>);

// erase_brand takes a unique view of a fresh brand by move, and it gives the
// unique view of the same element type and rank on the erased identity.  The
// constraint of the door refuses the brand of the result, so no view is
// erased two times.  The fixtures neg_core_strided_erase_brand_named and
// neg_core_strided_erase_brand_const hold the refusal of a named view and of
// a const view.
using ErasedView = decltype(::foundation::core::erase_brand(std::declval<StridedViewMut<int, 2, CheckBrand>&&>()));
static_assert(::foundation::brand::IsErased<ErasedView>);
static_assert(std::is_same_v<ErasedView::element_type, int> && ErasedView::static_rank == 2);
static_assert(!::foundation::brand::IsFreshBrand<ErasedView::brand_type>);

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
    auto [values, values_key] =
        mint_keyed_owner(fixed_array_owner<int, 12>, FixedArray<int, 12>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11});
    FixedArray<std::size_t, 2> const sizes{3, 4};
    FixedArray<std::size_t, 2> const strides{4, 1};
    return values.with_mut(values_key, [&sizes, &strides](auto run) noexcept {
        auto unique = mint_strided_view_mut(std::move(run), sizes.view(), strides.view(), 0).expect("the matrix fits");
        int total = 0;
        if (walk([&total](int& element) noexcept { total += element; }, unique).is_err()) return -1;
        return total;
    });
}
static_assert(matrix_sum() == 66);

// The walk over the branded view of a scope of the same matrix.
[[nodiscard]] consteval int scope_sum() noexcept {
    auto [values, values_key] =
        mint_keyed_owner(fixed_array_owner<int, 12>, FixedArray<int, 12>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11});
    FixedArray<std::size_t, 2> const sizes{3, 4};
    FixedArray<std::size_t, 2> const strides{4, 1};
    return values.with_mut(values_key, [&sizes, &strides](auto run) noexcept {
        auto unique = mint_strided_view_mut(std::move(run), sizes.view(), strides.view(), 0).expect("the matrix fits");
        return mint_strided_scope(unique, [](auto branded) noexcept {
            int total = 0;
            walk([&total](int& element) noexcept { total += element; }, branded);
            return total;
        });
    });
}
static_assert(scope_sum() == 66);

// The erased view of the same matrix stays the writer of its elements: the
// scope on it adds one to each element, and a walk of its view reads them.
[[nodiscard]] consteval int erased_scope_sum() noexcept {
    auto [values, values_key] =
        mint_keyed_owner(fixed_array_owner<int, 12>, FixedArray<int, 12>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11});
    FixedArray<std::size_t, 2> const sizes{3, 4};
    FixedArray<std::size_t, 2> const strides{4, 1};
    return values.with_mut(values_key, [&sizes, &strides](auto run) noexcept {
        auto erased = erase_brand(
            mint_strided_view_mut(std::move(run), sizes.view(), strides.view(), 0).expect("the matrix fits"));
        int const added = mint_strided_scope(erased, [](auto branded) noexcept {
            int count = 0;
            for (auto const index : branded.indices()) {
                branded.at(index) += 1;
                ++count;
            }
            return count;
        });
        int total = 0;
        if (walk([&total](int const& element) noexcept { total += element; }, erased.view()).is_err()) return -1;
        return added == 12 ? total : -1;
    });
}
static_assert(erased_scope_sum() == 78);

// A strided view refers to its elements (the transfer).  A walk writes each
// element through a const view of int, so a const view of
// elements that are not const is no shared read.  A view of const elements
// moves to another thread and two threads read it.
static_assert(::foundation::transfer::Sendable<StridedView<float const, 2>>
              && ::foundation::transfer::Shareable<StridedView<float const, 2>>);
static_assert(::foundation::transfer::Sendable<StridedView<float const, 2> const&>);
static_assert(!::foundation::transfer::Shareable<StridedView<float, 2>>
              && !::foundation::transfer::Sendable<StridedView<float, 2> const&>);

}  // namespace detail::strided_checks

}  // namespace foundation::core
