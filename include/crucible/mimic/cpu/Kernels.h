#pragma once

// The kernels of the CPU reference backend of Mimic, on the Kernel context.
//
// Each kernel is a constant object whose call takes the token of one call of
// run_kernel first (fixy/Ctx.h), then its output, then its inputs in their
// roles.  run_kernel checks the context before the call, so a kernel runs
// only on the thread of its FP scope, under the mode of its type.  Each
// kernel takes the IEEE mode: the round to nearest even, with no flush of a
// subnormal input or result.  A kernel allocates nothing, makes no system
// call and reads no clock.  It writes only its output.
//
//   multiply                  out = left * right, by the tree of
//                             foundation/core/Product.h
//   multiply_add_bias         out = left * right, then out + bias
//   clamp_below_at_zero       out = x < 0 ? +0 : x
//   zero_at_or_below_threshold
//                             out = gate <= threshold ? +0 : value
//   square_difference         out = (minuend - subtrahend)^2
//   divide_in_place           out = out / denominator
//   scale_product_of_difference
//                             out = (scale * (minuend - subtrahend)) * factor
//   fill                      out = value
//   add_scaled_in_place       out = fma(scale, other, out)
//   sum_all                   out = the tree sum of each element of the
//                             input, in row-major order
//   sum_leading               out[n] = the tree sum of input[0, n], input[1,
//                             n], and so on
//
// THE ORDER AND THE BOUNDS
//   Each kernel states the order of its rounds through the base form that it
//   calls, and the base header states the error bound of that order:
//   Product.h for multiply, sum_all and sum_leading, and Elementwise.h for
//   each element op.  The two kernels of two steps add the steps:
//     multiply_add_bias     the tree of the products, then one add of the
//                           bias: |y' - y| <= gamma(d + 2) * (sum_k
//                           |a[m, k] * b[k, n]| + |bias|) + the terms of
//                           the underflow of the products, with d =
//                           count_tree_depth(K)
//     sum_all               the tree of the n elements in row-major order:
//                           |y' - y| <= gamma(count_tree_depth(n)) * sum |x|
//   No result depends on the thread count, the split of the output, the
//   count of lanes or the host.  Each NaN of an arithmetic output is the
//   canonical NaN 0x7FC00000.  A select or a copy keeps the bits of its value.
//
// THE ROLES
//   Two inputs of one type take two roles of Side (foundation/core/Side.h),
//   so no two arguments of a call can swap.  A scalar of a kernel, such as a
//   threshold, also takes a role.  The roles of this header are in the
//   namespace side below.
//
// THE CHECKS
//   The views of a kernel must have the sizes of its op: a plan checks them
//   before it runs the kernel.  A kernel whose views do not agree ends the
//   process with the name of the kernel.  sum_all also needs an input whose
//   elements reshape to one axis, such as a dense view.
//
// THE TARGET
//   Each kernel calls a base form whose body reads the vector width of the
//   target, so the kernels are in the inline namespace CRUCIBLE_SIMD_TARGET
//   (THE TARGET NAMESPACE of foundation/Platform.h).

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/Elementwise.h>
#include <fixy/Reductions.h>
#include <foundation/Platform.h>
#include <foundation/core/FpMode.h>

#include <cstddef>
#include <utility>

namespace crucible::mimic::cpu {

// The roles of the inputs of the kernels.
namespace side {

// The bias row of a matrix product, broadcast to the sizes of the output.
struct Bias final {};
// The value that a gate lets through.
struct Value final {};
// The gate that a threshold compares.
struct Gate final {};
// The threshold of a gate.
struct Threshold final {};
// The two operands of a difference.
struct Minuend final {};
struct Subtrahend final {};
// The divisor of a quotient.
struct Denominator final {};
// The scale of a product, and the factor that multiplies it.
struct Scale final {};
struct Factor final {};
// The value of a fill.
struct FillValue final {};
// The input that a scaled add adds to the output.
struct Other final {};

}  // namespace side

// The FP mode of each kernel of this header.
template <::foundation::core::FpMode Mode>
concept IeeeMode = Mode == ::foundation::core::kFpModeIeee;

namespace detail {

// Ends the process for a kernel whose views do not agree (THE CHECKS).  The
// body is in src/mimic/cpu/Kernels.cpp.
[[noreturn]] CRUCIBLE_COLD void refuse_views_(::fixy::TextView kernel) noexcept;

inline namespace CRUCIBLE_SIMD_TARGET {

// The view of rank Rank of the one element of value, with each size of out
// and each stride zero: the broadcast of a scalar to the sizes of out.  The
// copy of the sizes reads the target, so the body is in the inline namespace.
template <std::size_t Rank, class Out>
[[nodiscard]] ::fixy::StridedView<float const, Rank> splat_(::fixy::View<float const, 1> value,
                                                            Out const& out) noexcept {
    ::fixy::FixedArray<std::size_t, Rank> sizes{};
    ::fixy::FixedArray<std::size_t, Rank> const strides{};
    bool const is_copied = ::fixy::copy(::fixy::View<std::size_t>{sizes.view()}, out.sizes()).is_ok();
    CRUCIBLE_FATAL_INVARIANT(is_copied);
    return ::fixy::mint_strided_view(value, sizes.view(), strides.view(), 0)
        .expect("each stride is zero, so the one element holds each index");
}

}  // namespace CRUCIBLE_SIMD_TARGET

}  // namespace detail

inline namespace CRUCIBLE_SIMD_TARGET {

// out = left * right.
struct Multiply final {
    template <class Brand, ::foundation::core::FpMode Mode, class OutBrand>
        requires IeeeMode<Mode>
    void operator()(::fixy::KernelCall<Brand, Mode> const& /*call*/, ::fixy::StridedViewMut<float, 2, OutBrand>& out,
                    ::fixy::LeftFactorOf<float> left, ::fixy::RightFactorOf<float> right) const noexcept {
        if (::fixy::multiply_by_tree(out, left, right).is_err()) [[unlikely]] {
            detail::refuse_views_(::fixy::TextView{"multiply"});
        }
    }
};

// out = left * right, then out + bias, where bias has the sizes of out.
struct MultiplyAddBias final {
    template <class Brand, ::foundation::core::FpMode Mode, class OutBrand>
        requires IeeeMode<Mode>
    void operator()(::fixy::KernelCall<Brand, Mode> const& /*call*/, ::fixy::StridedViewMut<float, 2, OutBrand>& out,
                    ::fixy::LeftFactorOf<float> left, ::fixy::RightFactorOf<float> right,
                    ::fixy::Side<side::Bias, ::fixy::StridedView<float const, 2>> bias) const noexcept {
        bool const is_multiplied = ::fixy::multiply_by_tree(out, left, right).is_ok();
        ::fixy::StridedView<float, 2> const products = out.view();
        bool const is_added =
            is_multiplied && ::fixy::write_pointwise(out, ::fixy::elementwise::Sum{}, products, bias.value()).is_ok();
        if (!is_added) [[unlikely]] {
            detail::refuse_views_(::fixy::TextView{"multiply_add_bias"});
        }
    }
};

// out = x < 0 ? +0 : x.
struct ClampBelowAtZero final {
    template <class Brand, ::foundation::core::FpMode Mode, std::size_t Rank, class OutBrand>
        requires IeeeMode<Mode>
    void operator()(::fixy::KernelCall<Brand, Mode> const& /*call*/, ::fixy::StridedViewMut<float, Rank, OutBrand>& out,
                    ::fixy::StridedView<float const, Rank> input) const noexcept {
        if (::fixy::write_pointwise(out, ::fixy::elementwise::ClampBelowAtZero{}, input).is_err()) [[unlikely]] {
            detail::refuse_views_(::fixy::TextView{"clamp_below_at_zero"});
        }
    }
};

// out = gate <= threshold ? +0 : value.
struct ZeroAtOrBelowThreshold final {
    template <class Brand, ::foundation::core::FpMode Mode, std::size_t Rank, class OutBrand>
        requires IeeeMode<Mode>
    void operator()(::fixy::KernelCall<Brand, Mode> const& /*call*/, ::fixy::StridedViewMut<float, Rank, OutBrand>& out,
                    ::fixy::Side<side::Value, ::fixy::StridedView<float const, Rank>> value,
                    ::fixy::Side<side::Gate, ::fixy::StridedView<float const, Rank>> gate,
                    ::fixy::Side<side::Threshold, float> threshold) const noexcept {
        ::fixy::FixedArray<float, 1> const one{threshold.value()};
        bool const is_written =
            ::fixy::write_pointwise(out, ::fixy::elementwise::ZeroAtOrBelowThreshold{}, value.value(), gate.value(),
                                    detail::splat_<Rank>(one.view(), out))
                .is_ok();
        if (!is_written) [[unlikely]] {
            detail::refuse_views_(::fixy::TextView{"zero_at_or_below_threshold"});
        }
    }
};

// out = (minuend - subtrahend)^2.
struct SquareDifference final {
    template <class Brand, ::foundation::core::FpMode Mode, std::size_t Rank, class OutBrand>
        requires IeeeMode<Mode>
    void operator()(::fixy::KernelCall<Brand, Mode> const& /*call*/, ::fixy::StridedViewMut<float, Rank, OutBrand>& out,
                    ::fixy::Side<side::Minuend, ::fixy::StridedView<float const, Rank>> minuend,
                    ::fixy::Side<side::Subtrahend, ::fixy::StridedView<float const, Rank>> subtrahend) const noexcept {
        bool const is_written =
            ::fixy::write_pointwise(out, ::fixy::elementwise::SquareOfDifference{}, minuend.value(), subtrahend.value())
                .is_ok();
        if (!is_written) [[unlikely]] {
            detail::refuse_views_(::fixy::TextView{"square_difference"});
        }
    }
};

// out = out / denominator.
struct DivideInPlace final {
    template <class Brand, ::foundation::core::FpMode Mode, std::size_t Rank, class OutBrand>
        requires IeeeMode<Mode>
    void operator()(::fixy::KernelCall<Brand, Mode> const& /*call*/, ::fixy::StridedViewMut<float, Rank, OutBrand>& out,
                    ::fixy::Side<side::Denominator, float> denominator) const noexcept {
        ::fixy::FixedArray<float, 1> const one{denominator.value()};
        ::fixy::StridedView<float, Rank> const numerators = out.view();
        bool const is_written = ::fixy::write_pointwise(out, ::fixy::elementwise::Quotient{}, numerators,
                                                        detail::splat_<Rank>(one.view(), out))
                                    .is_ok();
        if (!is_written) [[unlikely]] {
            detail::refuse_views_(::fixy::TextView{"divide_in_place"});
        }
    }
};

// out = (scale * (minuend - subtrahend)) * factor.
struct ScaleProductOfDifference final {
    template <class Brand, ::foundation::core::FpMode Mode, std::size_t Rank, class OutBrand>
        requires IeeeMode<Mode>
    void operator()(::fixy::KernelCall<Brand, Mode> const& /*call*/, ::fixy::StridedViewMut<float, Rank, OutBrand>& out,
                    ::fixy::Side<side::Minuend, ::fixy::StridedView<float const, Rank>> minuend,
                    ::fixy::Side<side::Subtrahend, ::fixy::StridedView<float const, Rank>> subtrahend,
                    ::fixy::Side<side::Scale, float> scale,
                    ::fixy::Side<side::Factor, ::fixy::StridedView<float const, Rank>> factor) const noexcept {
        ::fixy::FixedArray<float, 1> const one{scale.value()};
        bool const is_written =
            ::fixy::write_pointwise(out, ::fixy::elementwise::ScaledProductOfDifference{}, minuend.value(),
                                    subtrahend.value(), detail::splat_<Rank>(one.view(), out), factor.value())
                .is_ok();
        if (!is_written) [[unlikely]] {
            detail::refuse_views_(::fixy::TextView{"scale_product_of_difference"});
        }
    }
};

// out = value, with the bits of value.
struct Fill final {
    template <class Brand, ::foundation::core::FpMode Mode, std::size_t Rank, class OutBrand>
        requires IeeeMode<Mode>
    void operator()(::fixy::KernelCall<Brand, Mode> const& /*call*/, ::fixy::StridedViewMut<float, Rank, OutBrand>& out,
                    ::fixy::Side<side::FillValue, float> value) const noexcept {
        ::fixy::FixedArray<float, 1> const one{value.value()};
        if (::fixy::write_pointwise(out, ::fixy::elementwise::Copy{}, detail::splat_<Rank>(one.view(), out)).is_err())
            [[unlikely]] {
            detail::refuse_views_(::fixy::TextView{"fill"});
        }
    }
};

// out = fma(scale, other, out): one round of out + scale * other.
struct AddScaledInPlace final {
    template <class Brand, ::foundation::core::FpMode Mode, std::size_t Rank, class OutBrand>
        requires IeeeMode<Mode>
    void operator()(::fixy::KernelCall<Brand, Mode> const& /*call*/, ::fixy::StridedViewMut<float, Rank, OutBrand>& out,
                    ::fixy::Side<side::Scale, float> scale,
                    ::fixy::Side<side::Other, ::fixy::StridedView<float const, Rank>> other) const noexcept {
        ::fixy::FixedArray<float, 1> const one{scale.value()};
        ::fixy::StridedView<float, Rank> const addends = out.view();
        bool const is_written = ::fixy::write_pointwise(out, ::fixy::elementwise::FusedMultiplyAdd{},
                                                        detail::splat_<Rank>(one.view(), out), other.value(), addends)
                                    .is_ok();
        if (!is_written) [[unlikely]] {
            detail::refuse_views_(::fixy::TextView{"add_scaled_in_place"});
        }
    }
};

// out = the tree sum of each element of input in row-major order.  out holds
// one element.
struct SumAll final {
    template <class Brand, ::foundation::core::FpMode Mode, std::size_t Rank, class OutBrand, std::size_t InputRank>
        requires IeeeMode<Mode>
    void operator()(::fixy::KernelCall<Brand, Mode> const& /*call*/, ::fixy::StridedViewMut<float, Rank, OutBrand>& out,
                    ::fixy::StridedView<float const, InputRank> input) const noexcept {
        ::fixy::FixedArray<std::size_t, 2> const column{input.count(), 1};
        auto runs = input.reshape(column.view());
        if (runs.is_err() || out.count() != 1) [[unlikely]] {
            detail::refuse_views_(::fixy::TextView{"sum_all"});
        }
        ::fixy::StridedView<float const, 2> const one_run = std::move(runs).expect("the check above");
        ::fixy::FixedArray<float, 1> total{};
        ::fixy::FixedArray<std::size_t, 1> const sizes{1};
        ::fixy::FixedArray<std::size_t, 1> const strides{1};
        total = ::fixy::with_mut(std::move(total), [&](auto run) noexcept {
            auto target = ::fixy::mint_strided_view_mut(std::move(run), sizes.view(), strides.view(), 0)
                              .expect("one element fits its array");
            if (::fixy::sum_strided_runs_by_tree(target, one_run).is_err()) [[unlikely]] {
                detail::refuse_views_(::fixy::TextView{"sum_all"});
            }
        });
        if (::fixy::write_pointwise(out, ::fixy::elementwise::Copy{},
                                    detail::splat_<Rank>(::fixy::View<float const, 1>{total.view()}, out))
                .is_err()) [[unlikely]] {
            detail::refuse_views_(::fixy::TextView{"sum_all"});
        }
    }
};

// out[n] = the tree sum of input[0, n], input[1, n], and so on.
struct SumLeading final {
    template <class Brand, ::foundation::core::FpMode Mode, class OutBrand>
        requires IeeeMode<Mode>
    void operator()(::fixy::KernelCall<Brand, Mode> const& /*call*/, ::fixy::StridedViewMut<float, 1, OutBrand>& out,
                    ::fixy::StridedView<float const, 2> input) const noexcept {
        if (::fixy::sum_strided_runs_by_tree(out, input).is_err()) [[unlikely]] {
            detail::refuse_views_(::fixy::TextView{"sum_leading"});
        }
    }
};

inline constexpr Multiply multiply{};
inline constexpr MultiplyAddBias multiply_add_bias{};
inline constexpr ClampBelowAtZero clamp_below_at_zero{};
inline constexpr ZeroAtOrBelowThreshold zero_at_or_below_threshold{};
inline constexpr SquareDifference square_difference{};
inline constexpr DivideInPlace divide_in_place{};
inline constexpr ScaleProductOfDifference scale_product_of_difference{};
inline constexpr Fill fill{};
inline constexpr AddScaledInPlace add_scaled_in_place{};
inline constexpr SumAll sum_all{};
inline constexpr SumLeading sum_leading{};

}  // namespace CRUCIBLE_SIMD_TARGET

}  // namespace crucible::mimic::cpu
