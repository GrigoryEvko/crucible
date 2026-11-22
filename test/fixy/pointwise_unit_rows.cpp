// A unit of test_pointwise: the properties of the walk of rows of
// foundation/core/StridedRows.h.  pointwise_rig.h tells why the tests
// compile in more than one unit.

#include "pointwise_rig.h"

#include <cstdint>

namespace test_pointwise {

namespace {

using Sizes = ::fixy::FixedArray<std::size_t, 3>;

// The count of the parts of the rows of the walk of rows over a dense out of
// the sizes, a first input of the strides and a dense second input, when the
// walk writes the sum of the inputs at each index and gives its parts by the
// rule of the tiles of foundation/core/StridedRows.h, and zero otherwise.  The
// strides of the first input merge no two axes of the plan, so the plan has
// the three axes in their order, and the offset of a part in the dense out
// gives the number of its row and its first position.  Each part has at most
// row_tile_extent positions and the number of its row, and the parts of each
// row come in the order of the row and cover it.  A walk with no tile gives
// one part for each row.  The first input reaches at most 40000 elements.
[[nodiscard]] std::size_t parts_of_rows(Sizes const& sizes, Sizes const& first_strides) noexcept {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    std::size_t const rows = sizes.at(::fixy::index<0>()) * sizes.at(::fixy::index<1>());
    std::size_t const length = sizes.at(::fixy::index<2>());
    std::size_t const count = rows * length;
    std::size_t const reach = (sizes.at(::fixy::index<0>()) - 1) * first_strides.at(::fixy::index<0>())
                            + (sizes.at(::fixy::index<1>()) - 1) * first_strides.at(::fixy::index<1>())
                            + (length - 1) * first_strides.at(::fixy::index<2>()) + 1;
    auto const value_of = [](std::size_t position) noexcept { return static_cast<int>(position); };
    ::fixy::HeapArray<int> const first = ::fixy::mint_heap_array_with<int>(alloc, reach, value_of);
    ::fixy::HeapArray<int> const second = ::fixy::mint_heap_array_with<int>(
        alloc, count, [](std::size_t position) noexcept { return static_cast<int>(100000 * (position % 7)); });
    ::fixy::HeapArray<int> out = ::fixy::mint_heap_array(alloc, count, 0);
    ::fixy::HeapArray<std::size_t> next = ::fixy::mint_heap_array(alloc, rows, std::size_t{0});
    Sizes const dense{sizes.at(::fixy::index<1>()) * length, length, 1};
    auto const first_view =
        ::fixy::mint_strided_view(::fixy::View<int const>{first.view()}, sizes.view(), first_strides.view(), 0)
            .expect("the first input fits its run");
    auto const second_view =
        ::fixy::mint_strided_view(::fixy::View<int const>{second.view()}, sizes.view(), dense.view(), 0)
            .expect("the second input fits its run");
    bool is_ordered = true;
    std::size_t parts = 0;
    auto [is_walked, written] = ::fixy::with_mut(std::move(out), [&](auto run) noexcept {
        auto target = ::fixy::mint_strided_view_mut(std::move(run), sizes.view(), dense.view(), 0)
                          .expect("a dense out has no overlap");
        auto const body = [&](auto const& row, auto sums, auto const& left, auto const& right) noexcept {
            std::size_t const offset = row.offset(sums).value();
            std::size_t const number = offset / length;
            next.with_element(
                number,
                [&](std::size_t& start) noexcept {
                    is_ordered = is_ordered && row.index().value() == number && offset % length == start
                              && (row.count().value() <= ::foundation::core::row_tile_extent
                                  || row.count().value() == length);
                    start += row.count().value();
                },
                [] noexcept { ::fixy::fatal("a row of the plan"); });
            ++parts;
            for (auto const step : row.steps()) {
                sums.at(row.offset(sums), step) = left.at(row.offset(left), step) + right.at(row.offset(right), step);
            }
        };
        return ::foundation::core::mint_strided_rows(target, body, first_view, second_view).is_ok();
    });
    bool is_covered = true;
    for (std::size_t const start : next) {
        is_covered = is_covered && start == length;
    }
    bool is_summed = true;
    std::size_t position = 0;
    for (std::size_t outer = 0; outer < sizes.at(::fixy::index<0>()); ++outer) {
        for (std::size_t middle = 0; middle < sizes.at(::fixy::index<1>()); ++middle) {
            for (std::size_t inner = 0; inner < length; ++inner) {
                std::size_t const source = outer * first_strides.at(::fixy::index<0>())
                                         + middle * first_strides.at(::fixy::index<1>())
                                         + inner * first_strides.at(::fixy::index<2>());
                int const expected = *first.view().get(source).expect("inside the first input")
                                   + *second.view().get(position).expect("inside the second input");
                is_summed = is_summed && *written.view().get(position).expect("inside out") == expected;
                ++position;
            }
        }
    }
    bool const is_correct = is_walked && is_ordered && is_covered && is_summed;
    return is_correct ? parts : 0;
}

void test_the_row_tiles(::fixy::TestRunnerCtx const& test) noexcept {
    // Each row of 140 positions has nine parts: eight of 16 and one of 12.
    std::size_t const parts_of_a_row = 9;
    // The first input is the transpose of the last two axes, so the tile axis
    // is the axis just before the inner axis.
    CRUCIBLE_TEST_ASSERT(test, parts_of_rows(Sizes{2, 130, 140}, Sizes{130 * 140, 1, 130}) == 2 * 130 * parts_of_a_row);
    // The first input has a stride of one on the outer axis, so the tile axis
    // is the outer axis, and the axis between gives the weight of its number.
    // Its middle stride is not 130 * 140, so the last two axes do not merge.
    CRUCIBLE_TEST_ASSERT(test, parts_of_rows(Sizes{130, 2, 140}, Sizes{1, 130 * 141, 130}) == 130 * 2 * parts_of_a_row);
    // A plane of fewer than row_tile_plane positions takes no tile: each row
    // is one part.
    CRUCIBLE_TEST_ASSERT(test, parts_of_rows(Sizes{3, 70, 130}, Sizes{70 * 130, 1, 70}) == 3 * 70);
    pass(test, "  rows: the parts of the tiles cover each row in order: PASSED\n");
}

}  // namespace

void test_the_rows(::fixy::TestRunnerCtx const& test) noexcept {
    test_the_row_tiles(test);
    // The walk of rows gives the bits of walk() for each op, on the layouts of
    // the draw: dense, transposed, broadcast and stepped views, in place and
    // shared.
    constexpr Walker rows = Walker::Rows;
    pointwise_agrees_with_walk<float, 2, Add, rows>(test, "rows: float add", kSeed + 20, Add{});
    pointwise_agrees_with_walk<float, 3, MultiplyAdd, rows>(test, "rows: float multiply-add", kSeed + 21,
                                                            MultiplyAdd{});
    pointwise_agrees_with_walk<float, 2, Divide, rows>(test, "rows: float divide", kSeed + 22, Divide{});
    pointwise_agrees_with_walk<float, 1, Copy, rows>(test, "rows: float copy that keeps its bits", kSeed + 23, Copy{});
    pointwise_agrees_with_walk<double, 2, Add, rows>(test, "rows: double add", kSeed + 24, Add{});
    pointwise_agrees_with_walk<std::uint32_t, 2, Add, rows>(test, "rows: uint32 add", kSeed + 25, Add{});
    pointwise_agrees_with_walk<std::int64_t, 1, Negate, rows>(test, "rows: int64 negate", kSeed + 26, Negate{});
    pointwise_agrees_with_walk<std::uint16_t, 2, Add, rows>(test, "rows: uint16 add", kSeed + 27, Add{});
}

}  // namespace test_pointwise
