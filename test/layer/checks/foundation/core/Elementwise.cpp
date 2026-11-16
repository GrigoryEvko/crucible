// The compile-time checks of foundation/core/Elementwise.h.

#include <foundation/core/Elementwise.h>

#include <cstdint>

namespace foundation::core {

namespace detail::elementwise_checks {

namespace ops = ::foundation::core::elementwise;

template <class T>
[[nodiscard]] consteval bool same_bits(T left, T right) {
    return fp::total_key(left) == fp::total_key(right);
}

// Each op is a pointwise op of the walk.
static_assert(PointwiseOp<ops::ClampBelowAtZero, float, StridedView<float const, 2>>);
static_assert(PointwiseOp<ops::ZeroAtOrBelowThreshold, double, StridedView<double const, 1>,
                          StridedView<double const, 1>, StridedView<double const, 1>>);
static_assert(PointwiseOp<ops::FusedMultiplyAdd, float, StridedView<float const, 1>, StridedView<float const, 1>,
                          StridedView<float const, 1>>);

// Each op but the fused multiply-add has a lane form, so the walk takes its
// lane path.
static_assert(LaneOpOf<ops::ClampBelowAtZero, float, float> && LaneOpOf<ops::Copy, double, double>);
static_assert(LaneOpOf<ops::ZeroAtOrBelowThreshold, float, float, float, float>);
static_assert(LaneOpOf<ops::Sum, float, float, float> && LaneOpOf<ops::Quotient, double, double, double>);
static_assert(LaneOpOf<ops::SquareOfDifference, float, float, float>);
static_assert(LaneOpOf<ops::ScaledProductOfDifference, float, float, float, float, float>);
static_assert(!LaneOpOf<ops::FusedMultiplyAdd, float, float, float, float>);

// The selects and the copy keep the bits of a NaN, and each other op takes
// the canonical NaN of the walk.
static_assert(ops::ClampBelowAtZero::nan_rule == NanRule::KeepBits);
static_assert(ops::ZeroAtOrBelowThreshold::nan_rule == NanRule::KeepBits);
static_assert(ops::Copy::nan_rule == NanRule::KeepBits);
template <class Op>
concept StatesNanRule = requires { Op::nan_rule; };
static_assert(!StatesNanRule<ops::Sum> && !StatesNanRule<ops::FusedMultiplyAdd>);

// The values of the selects: -0 stays -0, a negative value gives +0, a NaN
// keeps its bits, and a NaN gate gives the value.
constexpr ops::ClampBelowAtZero clamp{};
constexpr ops::ZeroAtOrBelowThreshold gate{};
static_assert(same_bits(clamp(-2.0f), 0.0f) && same_bits(clamp(-0.0f), -0.0f) && same_bits(clamp(3.0), 3.0));
static_assert(same_bits(clamp(-min_subnormal_of<double>), 0.0));
static_assert(same_bits(clamp(-quiet_nan_of<float>), -quiet_nan_of<float>));
static_assert(same_bits(gate(5.0f, 0.0f, 0.0f), 0.0f) && same_bits(gate(5.0f, 1.0f, 0.0f), 5.0f));
static_assert(same_bits(gate(-0.0, quiet_nan_of<double>, 0.0), -0.0));

// The rounds of the two compound ops: 1e-30 * 1e-30 underflows in float, and
// the square of the difference rounds the difference first.
static_assert(same_bits(ops::SquareOfDifference{}(3.0f, 1.0f), 4.0f));
static_assert(same_bits(ops::ScaledProductOfDifference{}(3.0f, 1.0f, 0.5f, -2.0f), -2.0f));
static_assert(same_bits(ops::FusedMultiplyAdd{}(0.1, 10.0, -1.0), 0x1p-54));

}  // namespace detail::elementwise_checks

}  // namespace foundation::core
