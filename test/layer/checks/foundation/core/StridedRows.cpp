// The compile-time checks of foundation/core/StridedRows.h.

#include <foundation/core/StridedRows.h>

#include <foundation/core/Array.h>

#include <cstddef>
#include <type_traits>
#include <utility>

namespace foundation::core {

namespace detail::strided_rows_checks {

struct Walk final {};

// Only the door makes a step, an offset, an index or a count, so no caller
// makes one from a number.
static_assert(!std::is_constructible_v<RowStep<Walk>, std::size_t>);
static_assert(!std::is_constructible_v<RowOffset<RowViewBrand<Walk, 0>>, std::size_t>);
static_assert(!std::is_constructible_v<RowIndex<Walk>, std::size_t>);
static_assert(!std::is_constructible_v<RowCount<Walk>, std::size_t>);
// The offset of one view is not the offset of another view.
static_assert(!std::is_convertible_v<RowOffset<RowViewBrand<Walk, 1>>, RowOffset<RowViewBrand<Walk, 0>>>);
// A row does not copy, so it does not outlive the call of its body.
static_assert(!std::is_copy_constructible_v<StridedRow<Walk, 2>>);
static_assert(row_read_of_(1) == RowRead::Unit && row_read_of_(0) == RowRead::Strided);
static_assert(row_read_of_(2) == RowRead::Strided);
// No byte route builds a type of the walk, so no position, offset or view of a
// walk comes from bytes.
static_assert(!std::is_trivially_copyable_v<RowStep<Walk>>
              && !std::is_trivially_copyable_v<RowOffset<RowViewBrand<Walk, 0>>>);
static_assert(!std::is_trivially_copyable_v<RowView<int, Walk, 0, RowRead::Unit>>);
// The seal keeps the copy constructor trivial, so a step and an offset pass in
// registers.
static_assert(std::is_trivially_copy_constructible_v<RowStep<Walk>>);
static_assert(std::is_trivially_copy_constructible_v<RowOffset<RowViewBrand<Walk, 0>>>);

// The walk of rows in a constant evaluation: the stated row-major order, with
// a source of a column-major layout and the output in place.
[[nodiscard]] consteval bool walks_rows_in_constant_evaluation() noexcept {
    auto [values, values_key] = mint_keyed_owner(fixed_array_owner<int, 6>, FixedArray<int, 6>{1, 2, 3, 4, 5, 6});
    FixedArray<int, 6> const addends{10, 20, 30, 40, 50, 60};
    FixedArray<std::size_t, 2> const sizes{2, 3};
    FixedArray<std::size_t, 2> const strides{3, 1};
    FixedArray<std::size_t, 2> const columns{1, 2};
    StridedView<int const, 2> const source =
        mint_strided_view(addends.view(), sizes.view(), columns.view(), 0).expect("the matrix fits");
    std::size_t rows = 0;
    bool const is_written = values.with_mut(values_key, [&](auto run) noexcept {
        auto out = mint_strided_view_mut(std::move(run), sizes.view(), strides.view(), 0).expect("a dense matrix");
        StridedView<int const, 2> const in_place = out.view();
        return mint_strided_rows(
                   out,
                   [&rows](auto const& row, auto out_view, auto const& first, auto const& second) noexcept {
                       ++rows;
                       for (auto const step : row.steps()) {
                           out_view.at(row.offset(out_view), step) =
                               first.at(row.offset(first), step) + second.at(row.offset(second), step);
                       }
                   },
                   in_place, source)
            .is_ok();
    });
    // The source reads the addends in column-major order, and each of the
    // two rows of the matrix is one row of the walk.
    FixedArray<int, 6> const expected{11, 32, 53, 24, 45, 66};
    return is_written && rows == 2 && ::foundation::core::equal(values.view(), expected.view());
}
static_assert(walks_rows_in_constant_evaluation());

}  // namespace detail::strided_rows_checks

}  // namespace foundation::core
