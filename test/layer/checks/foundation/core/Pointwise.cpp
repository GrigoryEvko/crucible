// The compile-time checks of foundation/core/Pointwise.h.

#include <foundation/core/Pointwise.h>

#include <foundation/core/Array.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace foundation::core {

namespace detail::pointwise_checks {

// A generic op: a lane op for each element of the lane path.
struct Add final {
    template <class X>
    constexpr X operator()(X left, X right) const noexcept {
        return static_cast<X>(left + right);
    }
};

// An op with a member is no pointwise op: it could reach a run.
struct Scaled final {
    float factor = 2.0f;
    constexpr float operator()(float value) const noexcept { return value * factor; }
};

using Floats = StridedView<float const, 2>;
using Shorts = StridedView<std::uint16_t const, 2>;

static_assert(PointwiseOp<Add, float, Floats, Floats>);
static_assert(!PointwiseOp<Scaled, float, Floats>);
static_assert(LaneOpOf<Add, float, float, float>);
static_assert(LaneOpOf<Add, double, double, double>);
static_assert(LaneOpOf<Add, std::int64_t, std::int64_t, std::int64_t>);
// A narrow integer computes in int in a scalar step, so it is no lane element.
static_assert(!LaneOpOf<Add, std::uint16_t, std::uint16_t, std::uint16_t>);
static_assert(PointwiseOp<Add, std::uint16_t, Shorts, Shorts>);
// Lanes of different widths do not fill one register each.
static_assert(!LaneOpOf<Add, float, double, double>);
// A lane op gives a register of the output element.
static_assert(std::is_same_v<std::invoke_result_t<Add const&, ::foundation::simd::vec<float, pointwise_lanes<float>>,
                                                  ::foundation::simd::vec<float, pointwise_lanes<float>>>,
                             ::foundation::simd::vec<float, pointwise_lanes<float>>>);
static_assert(pointwise_lanes<float> == 16 && pointwise_lanes<double> == 8);

// The test of no overlap of an input with the output, the order of the axes
// and the axis of the tile path.
static_assert(lane_class_of_(0) == LaneClass::Splat && lane_class_of_(1) == LaneClass::Dense);
static_assert(lane_class_of_(2) == LaneClass::StepTwo && lane_class_of_(3) == LaneClass::General);

// The walk in a constant evaluation: the stated row-major order, with the op
// on lanes of one live element.  The second input is the output in place.
[[nodiscard]] consteval bool walks_in_constant_evaluation() noexcept {
    FixedArray<int, 6> values{1, 2, 3, 4, 5, 6};
    FixedArray<int, 6> const addends{10, 20, 30, 40, 50, 60};
    FixedArray<std::size_t, 2> const sizes{2, 3};
    FixedArray<std::size_t, 2> const strides{3, 1};
    FixedArray<std::size_t, 2> const columns{1, 2};
    StridedView<int const, 2> const source =
        mint_strided_view(addends.view(), sizes.view(), columns.view(), 0).expect("the matrix fits");
    auto [is_written, kept] = with_mut(std::move(values), [&](auto run) noexcept {
        auto out = mint_strided_view_mut(std::move(run), sizes.view(), strides.view(), 0).expect("a dense matrix");
        StridedView<int const, 2> const in_place = out.view();
        return write_pointwise(out, Add{}, in_place, source).is_ok();
    });
    // The source reads the addends in column-major order.
    return is_written && kept == FixedArray<int, 6>{11, 32, 53, 24, 45, 66};
}
static_assert(walks_in_constant_evaluation());

}  // namespace detail::pointwise_checks

}  // namespace foundation::core
