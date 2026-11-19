#pragma once

// The calls of the kernels of crucible/mimic/cpu/Kernels.h for the tests, and
// the digest of their outputs on fixed draws.
//
// test_mimic_cpu_kernels and its units of the levels x86-64-v3 and x86-64-v4
// include this header.  Each name is in an unnamed namespace, so each unit
// keeps its own body, compiled for its own target.  The kernels state each
// order, so each unit gives the same digest.
//
// The walk of scale_product_of_difference has four inputs, and it has more
// machine code than each other kernel together.  Thus each unit of a level
// has a second unit for that kernel alone, which gives the function
// scale_product_of_difference_in_*_unit below.

#include <crucible/mimic/cpu/Kernels.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <foundation/core/FpMode.h>
#include <foundation/os/FpControl.h>

#include "foundation/philox_stream.h"
#include "product_rig.h"
#include "test_assert.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace crucible::test {

// The digests of the fixed draws of the kernels.
struct KernelDigests {
    std::uint64_t products = 0;
    std::uint64_t elements = 0;
    std::uint64_t sums = 0;
};

// The brand of the kernel contexts of the tests.
struct KernelTestBrand final {};

// The units of test_mimic_cpu_kernels: the unit of the test, which takes the
// level of the build, and the units of the levels x86-64-v3 and x86-64-v4.
enum class KernelUnit : std::uint8_t {
    Build,
    V3,
    V4,
};

using KernelCtx = ::fixy::Kernel<KernelTestBrand, ::foundation::core::kFpModeIeee>;

// The inputs of scale_product_of_difference, in their roles.
using ScaledMinuend = ::fixy::Side<::crucible::mimic::cpu::side::Minuend, ::fixy::StridedView<float const, 2>>;
using ScaledSubtrahend = ::fixy::Side<::crucible::mimic::cpu::side::Subtrahend, ::fixy::StridedView<float const, 2>>;
using ScaledScale = ::fixy::Side<::crucible::mimic::cpu::side::Scale, float>;
using ScaledFactor = ::fixy::Side<::crucible::mimic::cpu::side::Factor, ::fixy::StridedView<float const, 2>>;

// The kernel scale_product_of_difference into out under ctx, in the unit of
// the build and in the units of the levels.  Each body is in a unit of its
// own, with the target of its unit.
void scale_product_of_difference_in_build_unit(KernelCtx const& ctx, Matrix<float>& out, ScaledMinuend minuend,
                                               ScaledSubtrahend subtrahend, ScaledScale scale,
                                               ScaledFactor factor) noexcept;
void scale_product_of_difference_in_v3_unit(KernelCtx const& ctx, Matrix<float>& out, ScaledMinuend minuend,
                                            ScaledSubtrahend subtrahend, ScaledScale scale,
                                            ScaledFactor factor) noexcept;
void scale_product_of_difference_in_v4_unit(KernelCtx const& ctx, Matrix<float>& out, ScaledMinuend minuend,
                                            ScaledSubtrahend subtrahend, ScaledScale scale,
                                            ScaledFactor factor) noexcept;

namespace {

namespace cpu = ::crucible::mimic::cpu;
namespace core = ::foundation::core;
namespace os = ::foundation::os;

// Runs body with a kernel context of the IEEE mode on the calling thread.
template <class Body>
void with_kernel_context(Body&& body) noexcept {
    os::FpScope<core::kFpModeIeee> const scope = os::mint_fp_scope<core::kFpModeIeee>();
    KernelCtx const ctx = ::fixy::mint_kernel_context<KernelTestBrand>(scope);
    static_cast<Body&&>(body)(ctx);
}

// Runs the kernel into the matrix out of rank 2 under ctx, with the inputs.
template <class Kernel, class... Inputs>
void run_into(KernelCtx const& ctx, Matrix<float>& out, Kernel const& kernel, Inputs&&... inputs) noexcept {
    ::fixy::FixedArray<std::size_t, 2> const sizes{out.shape.rows, out.shape.columns};
    ::fixy::FixedArray<std::size_t, 2> const strides{out.place.row_stride, out.place.column_stride};
    std::size_t const offset = out.place.offset;
    out.values = ::fixy::with_mut(std::move(out.values), [&](auto run) noexcept {
        auto target = ::fixy::mint_strided_view_mut(std::move(run), sizes.view(), strides.view(), offset)
                          .expect("the output fits its array");
        ::fixy::run_kernel(ctx, kernel, target, static_cast<Inputs&&>(inputs)...);
    });
}

// Runs the kernel into the one row of out as a view of rank 1.
template <class Kernel, class... Inputs>
void run_into_row(KernelCtx const& ctx, Matrix<float>& out, Kernel const& kernel, Inputs&&... inputs) noexcept {
    ::fixy::FixedArray<std::size_t, 1> const sizes{out.shape.columns};
    ::fixy::FixedArray<std::size_t, 1> const strides{out.place.column_stride};
    std::size_t const offset = out.place.offset;
    out.values = ::fixy::with_mut(std::move(out.values), [&](auto run) noexcept {
        auto target = ::fixy::mint_strided_view_mut(std::move(run), sizes.view(), strides.view(), offset)
                          .expect("the output fits its array");
        ::fixy::run_kernel(ctx, kernel, target, static_cast<Inputs&&>(inputs)...);
    });
}

// The kernel scale_product_of_difference into out under ctx, with the body of
// the unit Unit.
template <KernelUnit Unit>
void run_scale_product_of_difference(KernelCtx const& ctx, Matrix<float>& out, ScaledMinuend minuend,
                                     ScaledSubtrahend subtrahend, ScaledScale scale, ScaledFactor factor) noexcept {
    if constexpr (Unit == KernelUnit::V3) {
        scale_product_of_difference_in_v3_unit(ctx, out, minuend, subtrahend, scale, factor);
    } else if constexpr (Unit == KernelUnit::V4) {
        scale_product_of_difference_in_v4_unit(ctx, out, minuend, subtrahend, scale, factor);
    } else {
        scale_product_of_difference_in_build_unit(ctx, out, minuend, subtrahend, scale, factor);
    }
}

// The view of a row of N elements broadcast to the sizes [rows, N].
[[nodiscard]] inline ::fixy::StridedView<float const, 2> broadcast_row(Matrix<float> const& row,
                                                                       std::size_t rows) noexcept {
    ::fixy::FixedArray<std::size_t, 1> const sizes{row.shape.columns};
    ::fixy::FixedArray<std::size_t, 1> const strides{row.place.column_stride};
    ::fixy::FixedArray<std::size_t, 2> const target{rows, row.shape.columns};
    return ::fixy::mint_strided_view(::fixy::View<float const>{row.values.view()}, sizes.view(), strides.view(),
                                     row.place.offset)
        .expect("the row fits its array")
        .broadcast_to(target.view())
        .expect("a row broadcasts to the rows");
}

template <class Value>
[[nodiscard]] std::uint64_t fold_matrix(std::uint64_t digest, Matrix<Value> const& matrix) noexcept {
    for (std::size_t row = 0; row < matrix.shape.rows; ++row) {
        for (std::size_t column = 0; column < matrix.shape.columns; ++column) {
            digest = fold_value(digest, matrix.at(row, column));
        }
    }
    return digest;
}

// The digests of each kernel on fixed draws in the shapes of the first
// model: a batch of 8, a layer of 16 to 24 and a layer of 24 to 4, with the
// bodies of the unit Unit.
template <KernelUnit Unit>
[[nodiscard]] KernelDigests kernel_digests_of_this_unit() noexcept {
    PhiloxStream stream{0x6E1C000000000001u};
    KernelDigests digests{kDigestOffset, kDigestOffset, kDigestOffset};
    Matrix<float> input = make_matrix<float>(MatrixShape{8, 16}, Layout::RowMajor, 0);
    Matrix<float> weight = make_matrix<float>(MatrixShape{24, 16}, Layout::RowMajor, 0);
    Matrix<float> bias = make_matrix<float>(MatrixShape{1, 24}, Layout::RowMajor, 0);
    Matrix<float> gradient = make_matrix<float>(MatrixShape{8, 24}, Layout::RowMajor, 0);
    fill_matrix(input, stream, 6);
    fill_matrix(weight, stream, 6);
    fill_matrix(bias, stream, 6);
    fill_matrix(gradient, stream, 6);
    Matrix<float> forward = make_matrix<float>(MatrixShape{8, 24}, Layout::RowMajor, 0);
    Matrix<float> input_gradient = make_matrix<float>(MatrixShape{8, 16}, Layout::RowMajor, 0);
    Matrix<float> weight_gradient = make_matrix<float>(MatrixShape{24, 16}, Layout::RowMajor, 0);
    Matrix<float> clamped = make_matrix<float>(MatrixShape{8, 24}, Layout::RowMajor, 0);
    Matrix<float> gated = make_matrix<float>(MatrixShape{8, 24}, Layout::RowMajor, 0);
    Matrix<float> squares = make_matrix<float>(MatrixShape{8, 24}, Layout::RowMajor, 0);
    Matrix<float> scaled = make_matrix<float>(MatrixShape{8, 24}, Layout::RowMajor, 0);
    Matrix<float> updated = make_matrix<float>(MatrixShape{24, 16}, Layout::RowMajor, 0);
    Matrix<float> total = make_matrix<float>(MatrixShape{1, 1}, Layout::RowMajor, 0);
    Matrix<float> column_sums = make_matrix<float>(MatrixShape{1, 24}, Layout::RowMajor, 0);
    with_kernel_context([&](KernelCtx const& ctx) noexcept {
        auto const transposed_weight = weight.view().transpose(::fixy::index<0>(), ::fixy::index<1>());
        run_into(ctx, forward, cpu::multiply_add_bias, ::fixy::side_as<::fixy::side::LeftFactor>(input.view()),
                 ::fixy::side_as<::fixy::side::RightFactor>(transposed_weight),
                 ::fixy::side_as<cpu::side::Bias>(broadcast_row(bias, 8)));
        run_into(ctx, input_gradient, cpu::multiply, ::fixy::side_as<::fixy::side::LeftFactor>(gradient.view()),
                 ::fixy::side_as<::fixy::side::RightFactor>(weight.view()));
        auto const transposed_gradient = gradient.view().transpose(::fixy::index<0>(), ::fixy::index<1>());
        run_into(ctx, weight_gradient, cpu::multiply, ::fixy::side_as<::fixy::side::LeftFactor>(transposed_gradient),
                 ::fixy::side_as<::fixy::side::RightFactor>(input.view()));
        run_into(ctx, clamped, cpu::clamp_below_at_zero, forward.view());
        run_into(ctx, gated, cpu::zero_at_or_below_threshold, ::fixy::side_as<cpu::side::Value>(gradient.view()),
                 ::fixy::side_as<cpu::side::Gate>(forward.view()), ::fixy::side_as<cpu::side::Threshold>(0.0f));
        run_into(ctx, squares, cpu::square_difference, ::fixy::side_as<cpu::side::Minuend>(forward.view()),
                 ::fixy::side_as<cpu::side::Subtrahend>(gradient.view()));
        run_scale_product_of_difference<Unit>(ctx, scaled, ::fixy::side_as<cpu::side::Minuend>(forward.view()),
                                              ::fixy::side_as<cpu::side::Subtrahend>(gradient.view()),
                                              ::fixy::side_as<cpu::side::Scale>(0.0625f),
                                              ::fixy::side_as<cpu::side::Factor>(broadcast_row(bias, 8)));
        updated.values =
            ::fixy::mint_heap_array_copy<float>(rig_alloc(), ::fixy::View<float const>{weight.values.view()});
        run_into(ctx, updated, cpu::add_scaled_in_place, ::fixy::side_as<cpu::side::Scale>(-0.01f),
                 ::fixy::side_as<cpu::side::Other>(weight_gradient.view()));
        run_into(ctx, total, cpu::sum_all, squares.view());
        run_into(ctx, total, cpu::divide_in_place, ::fixy::side_as<cpu::side::Denominator>(192.0f));
        run_into_row(ctx, column_sums, cpu::sum_leading, gradient.view());
    });
    digests.products =
        fold_matrix(fold_matrix(fold_matrix(digests.products, forward), input_gradient), weight_gradient);
    digests.elements = fold_matrix(
        fold_matrix(fold_matrix(fold_matrix(fold_matrix(digests.elements, clamped), gated), squares), scaled), updated);
    digests.sums = fold_matrix(fold_matrix(digests.sums, total), column_sums);
    return digests;
}

}  // namespace

}  // namespace crucible::test
