// A unit of test_pointwise: the properties of the walk of rows of
// foundation/core/StridedRows.h.  pointwise_rig.h tells why the tests
// compile in more than one unit.

#include "pointwise_rig.h"

#include <cstdint>

namespace test_pointwise {

void test_the_rows(::fixy::TestRunnerCtx const& test) noexcept {
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
