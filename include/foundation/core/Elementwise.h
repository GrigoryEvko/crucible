#pragma once

// The element family: the ops of one element of the pointwise walk of
// foundation/core/Pointwise.h, each with its stated order of rounds and its
// error bound.
//
//   ClampBelowAtZero(x)                  x < 0 ? +0 : x
//   ZeroAtOrBelowThreshold(v, g, t)      g <= t ? +0 : v
//   Copy(x)                              x
//   Sum(a, b)                            a + b
//   Quotient(a, b)                       a / b
//   SquareOfDifference(a, b)             d * d, with d = a - b
//   ScaledProductOfDifference(a, b, s, f)
//                                        (s * (a - b)) * f
//   FusedMultiplyAdd(s, f, c)            s * f + c with one round (fp::fma)
//
// Each op is an empty final class, a PointwiseOp of the walk.  It takes the
// elements of its inputs in the order of the list, and the walk gives them in
// the order of the inputs of write_pointwise.  A call of an op on elements
// and a call on lanes give the same bits, because each lane computes the same
// IEEE operations in the same order.  FusedMultiplyAdd has no lane form, so
// the walk takes its scalar path.
//
// THE ORDER OF THE ROUNDS
//   Each operation rounds to the type of the elements with the round to
//   nearest even, and the build contracts no multiply and add.  The order of
//   the operations is the order of the parentheses of the list.
//
// THE NAN RULE
//   ClampBelowAtZero, ZeroAtOrBelowThreshold and Copy select or copy a value,
//   so they state NanRule::KeepBits: a NaN keeps its bits.  A compare of a NaN
//   is false, so ClampBelowAtZero gives a NaN for a NaN, and
//   ZeroAtOrBelowThreshold gives v for a NaN of g or t.  Each other op is
//   arithmetic, and the walk gives the canonical NaN for each of its NaN
//   outputs (THE NAN RULE of Pointwise.h).
//
// THE ERROR BOUNDS
//   With the unit roundoff u (2^-24 for a float, 2^-53 for a double), gamma(j)
//   = j * u / (1 - j * u), eta the largest error of a result that underflows
//   (2^-150 for a float, 2^-1075 for a double), and y the exact value of the
//   expression of the list:
//     ClampBelowAtZero, ZeroAtOrBelowThreshold, Copy
//                                        exact
//     Sum, Quotient, FusedMultiplyAdd    the IEEE result: one round of y.  So
//                                        |y' - y| <= u * |y|, and <= eta when
//                                        the result is subnormal.  An add with
//                                        a subnormal result is exact
//     SquareOfDifference                 |y' - y| <= gamma(3) * y + eta
//     ScaledProductOfDifference          |y' - y| <= gamma(3) * |y|
//                                                    + eta * (1 + u) * |f|
//                                                    + eta
//   A step past the largest finite value gives an infinity, and the bounds
//   hold when no step overflows.  test_core_elementwise checks each bound
//   against an oracle of a higher precision.
//
// THE TARGET
//   The lane forms read the vector types of the target, so the ops are in the
//   inline namespace CRUCIBLE_SIMD_TARGET (THE TARGET NAMESPACE of
//   foundation/Platform.h).

#include <foundation/Simd.h>
#include <foundation/core/Float.h>
#include <foundation/core/Number.h>
#include <foundation/core/Pointwise.h>

namespace foundation::core::elementwise {

inline namespace CRUCIBLE_SIMD_TARGET {

namespace detail {

// The lanes of on_true where the mask is set, and of on_false elsewhere.  The
// conditional of the vector extensions of GCC moves the bits of the floats, so
// each lane keeps the bits of its value, a NaN payload too.
template <class T, int Lanes>
[[nodiscard]] CRUCIBLE_INLINE simd::vec<T, Lanes> select_lanes_(typename simd::vec<T, Lanes>::mask_type mask,
                                                                simd::vec<T, Lanes> on_true,
                                                                simd::vec<T, Lanes> on_false) noexcept {
    return simd::vec<T, Lanes>{mask.raw() ? on_true.raw() : on_false.raw()};
}

}  // namespace detail

// x < 0 ? +0 : x.  -0 gives -0, and a NaN gives itself.
struct ClampBelowAtZero final {
    static constexpr NanRule nan_rule = NanRule::KeepBits;

    template <Float T>
    [[nodiscard]] constexpr T operator()(T value) const noexcept {
        return value < T{0} ? T{0} : value;
    }

    template <Float T, int Lanes>
    [[nodiscard]] CRUCIBLE_INLINE simd::vec<T, Lanes> operator()(simd::vec<T, Lanes> value) const noexcept {
        using Vector = simd::vec<T, Lanes>;
        return detail::select_lanes_<T, Lanes>(value < Vector{T{0}}, Vector{T{0}}, value);
    }
};

// gate <= threshold ? +0 : value.  A NaN gate or threshold gives value.
struct ZeroAtOrBelowThreshold final {
    static constexpr NanRule nan_rule = NanRule::KeepBits;

    template <Float T>
    [[nodiscard]] constexpr T operator()(T value, T gate, T threshold) const noexcept {
        return gate <= threshold ? T{0} : value;
    }

    template <Float T, int Lanes>
    [[nodiscard]] CRUCIBLE_INLINE simd::vec<T, Lanes> operator()(simd::vec<T, Lanes> value, simd::vec<T, Lanes> gate,
                                                                 simd::vec<T, Lanes> threshold) const noexcept {
        using Vector = simd::vec<T, Lanes>;
        return detail::select_lanes_<T, Lanes>(gate <= threshold, Vector{T{0}}, value);
    }
};

// value, with its bits.
struct Copy final {
    static constexpr NanRule nan_rule = NanRule::KeepBits;

    template <class X>
    [[nodiscard]] constexpr X operator()(X value) const noexcept {
        return value;
    }
};

// left + right.
struct Sum final {
    template <class X>
    [[nodiscard]] constexpr X operator()(X left, X right) const noexcept {
        return left + right;
    }
};

// numerator / denominator.
struct Quotient final {
    template <class X>
    [[nodiscard]] constexpr X operator()(X numerator, X denominator) const noexcept {
        return numerator / denominator;
    }
};

// (left - right) * (left - right), with one round of the difference.
struct SquareOfDifference final {
    template <class X>
    [[nodiscard]] constexpr X operator()(X left, X right) const noexcept {
        X const difference = left - right;
        return difference * difference;
    }
};

// (scale * (left - right)) * factor.
struct ScaledProductOfDifference final {
    template <class X>
    [[nodiscard]] constexpr X operator()(X left, X right, X scale, X factor) const noexcept {
        X const difference = left - right;
        X const scaled = scale * difference;
        return scaled * factor;
    }
};

// scale * factor + addend with one round, by fp::fma: the instruction where
// the target has it, and the exact form in integers on each other target
// (THE FMA of foundation/core/Float.h).
struct FusedMultiplyAdd final {
    template <Float T>
    [[nodiscard]] constexpr T operator()(T scale, T factor, T addend) const noexcept {
        return fp::fma(scale, factor, addend);
    }
};

}  // namespace CRUCIBLE_SIMD_TARGET

}  // namespace foundation::core::elementwise
