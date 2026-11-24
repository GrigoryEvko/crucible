// Tests of the kernels of crucible/mimic/cpu/Kernels.h on the Kernel context.
//
// Each kernel runs through run_kernel under an FP scope of the IEEE mode.  The
// test compares each output with an oracle outside the code under test:
//   - the products: the scalar model of the tree of test/product_rig.h, bit
//     for bit, and the exact sum of test/exact_sum.h within the bound of
//     Kernels.h, in the three layouts of the first model: the input times the
//     transposed weight, the gradient times the weight, and the transposed
//     gradient times the input
//   - a split of the rows of a product over 1, 2, 3 and 4 threads, each with
//     its own scope and context, gives the bits of one thread
//   - the selects and the fill: their stated bits, NaN payloads too
//   - the arithmetic ops: the exact oracle or the double oracle within the
//     bound of foundation/core/Elementwise.h
//   - the sums: the model of the tree bit for bit, and the exact sum within
//     the bound, and the mean of the squared differences of the loss within
//     its composite bound
//   - the digests of the fixed draws hold in this unit and in the units of
//     x86-64-v3 and x86-64-v4 that the build has: the units of each level
//     other than the level of the build (mimic_cpu_kernel_units.h)
// The inputs come from fixed Philox seeds, with the hard cases: the two
// zeros, the subnormal values, the infinities, NaNs with payloads, and
// cancellations.

#include <crucible/mimic/cpu/Kernels.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/effects/Ctx.h>
#include <foundation/os/X86Level.h>

#include "exact_sum.h"
#include "foundation/philox_stream.h"
#include "mimic_cpu_kernel_forms.h"
#include "mimic_cpu_kernel_units.h"
#include "product_rig.h"
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace {

namespace cpu = ::crucible::mimic::cpu;
namespace eff = ::foundation::effects;

using ::crucible::test::bits_of_float;
using ::crucible::test::BoundRecord;
using ::crucible::test::broadcast_row;
using ::crucible::test::ExactSum;
using ::crucible::test::KernelCtx;
using ::crucible::test::KernelDigests;
using ::crucible::test::KernelUnit;
using ::crucible::test::Layout;
using ::crucible::test::make_matrix;
using ::crucible::test::Matrix;
using ::crucible::test::MatrixShape;
using ::crucible::test::PhiloxStream;
using ::crucible::test::run_into;
using ::crucible::test::run_into_row;
using ::crucible::test::with_kernel_context;

[[nodiscard]] bool same_bits(float left, float right) noexcept { return bits_of_float(left) == bits_of_float(right); }

// A drawn value: about one in eight is a hard case.
[[nodiscard]] float draw_hard_value(PhiloxStream& stream) noexcept {
    switch (stream.below(16)) {
        case 0:
            return 0.0f;
        case 1:
            return -0.0f;
        case 2:
            return ::fixy::min_subnormal_of<float> * static_cast<float>(1 + stream.below(1000));
        case 3:
            return (stream.below(2) == 0 ? 1.0f : -1.0f) * ::fixy::infinity_of<float>;
        case 4:
            return ::fixy::bit_cast<float>(0xFFC00000u | (stream.next_word() & 0x3FFFFFu));
        default:
            return ::crucible::test::draw_spread_value<float>(stream, 20);
    }
}

[[nodiscard]] Matrix<float> drawn_matrix(PhiloxStream& stream, MatrixShape shape, Layout layout) noexcept {
    Matrix<float> matrix = make_matrix<float>(shape, layout, 1);
    ::crucible::test::edit_matrix(
        matrix, [&stream](std::size_t, std::size_t, float& value) noexcept { value = draw_hard_value(stream); });
    return matrix;
}

// The products of the first model in its three layouts, against the model of
// the tree and within the bound of the exact sum, with and without a bias.
void test_the_products_of_the_first_model() {
    PhiloxStream stream{0x6E1C000000000010u};
    BoundRecord record{};
    for (std::size_t round = 0; round < 40; ++round) {
        Matrix<float> input = make_matrix<float>(MatrixShape{8, 16}, Layout::RowMajor, 0);
        Matrix<float> weight = make_matrix<float>(MatrixShape{24, 16}, Layout::RowMajor, 0);
        Matrix<float> bias = make_matrix<float>(MatrixShape{1, 24}, Layout::RowMajor, 0);
        Matrix<float> gradient = make_matrix<float>(MatrixShape{8, 24}, Layout::RowMajor, 0);
        ::crucible::test::fill_matrix(input, stream, 30);
        ::crucible::test::fill_matrix(weight, stream, 30);
        ::crucible::test::fill_matrix(bias, stream, 30);
        ::crucible::test::fill_matrix(gradient, stream, 30);
        // The transposed views as matrices of the rig, for the model.
        Matrix<float> weight_transposed = make_matrix<float>(MatrixShape{16, 24}, Layout::ColumnMajor, 0);
        weight_transposed.values = ::fixy::mint_heap_array_copy<float>(::crucible::test::rig_alloc(),
                                                                       ::fixy::View<float const>{weight.values.view()});
        Matrix<float> gradient_transposed = make_matrix<float>(MatrixShape{24, 8}, Layout::ColumnMajor, 0);
        gradient_transposed.values = ::fixy::mint_heap_array_copy<float>(
            ::crucible::test::rig_alloc(), ::fixy::View<float const>{gradient.values.view()});
        Matrix<float> forward = make_matrix<float>(MatrixShape{8, 24}, Layout::RowMajor, 0);
        Matrix<float> input_gradient = make_matrix<float>(MatrixShape{8, 16}, Layout::RowMajor, 0);
        Matrix<float> weight_gradient = make_matrix<float>(MatrixShape{24, 16}, Layout::RowMajor, 0);
        with_kernel_context([&](KernelCtx const& ctx) noexcept {
            run_into(ctx, forward, cpu::multiply_add_bias, ::fixy::side_as<::fixy::side::LeftFactor>(input.view()),
                     ::fixy::side_as<::fixy::side::RightFactor>(weight_transposed.view()),
                     ::fixy::side_as<cpu::side::Bias>(broadcast_row(bias, 8)));
            run_into(ctx, input_gradient, cpu::multiply, ::fixy::side_as<::fixy::side::LeftFactor>(gradient.view()),
                     ::fixy::side_as<::fixy::side::RightFactor>(weight.view()));
            run_into(ctx, weight_gradient, cpu::multiply,
                     ::fixy::side_as<::fixy::side::LeftFactor>(gradient_transposed.view()),
                     ::fixy::side_as<::fixy::side::RightFactor>(input.view()));
        });
        for (std::size_t row = 0; row < 8; ++row) {
            for (std::size_t column = 0; column < 24; ++column) {
                float const product = ::crucible::test::model_product(input, weight_transposed, row, column);
                float const expected = ::crucible::test::canonical_output(product + bias.at(0, column));
                CRUCIBLE_TEST_ASSERT(kTest, same_bits(forward.at(row, column), expected));
                auto [exact, magnitude] = ::crucible::test::exact_product(input, weight_transposed, row, column);
                exact.add(bias.at(0, column));
                magnitude.add(::fixy::fp::abs(bias.at(0, column)));
                // The tree of 16 products has depth 4, and the add of the bias is
                // one more round on the path of each product.
                ::crucible::test::check_bound(kTest, exact, magnitude, 16, 2, forward.at(row, column), record);
            }
            for (std::size_t column = 0; column < 16; ++column) {
                CRUCIBLE_TEST_ASSERT(kTest, same_bits(input_gradient.at(row, column),
                                                      ::crucible::test::model_product(gradient, weight, row, column)));
                auto const [exact, magnitude] = ::crucible::test::exact_product(gradient, weight, row, column);
                ::crucible::test::check_bound(kTest, exact, magnitude, 24, 1, input_gradient.at(row, column), record);
            }
        }
        for (std::size_t row = 0; row < 24; ++row) {
            for (std::size_t column = 0; column < 16; ++column) {
                CRUCIBLE_TEST_ASSERT(
                    kTest, same_bits(weight_gradient.at(row, column),
                                     ::crucible::test::model_product(gradient_transposed, input, row, column)));
                auto const [exact, magnitude] =
                    ::crucible::test::exact_product(gradient_transposed, input, row, column);
                ::crucible::test::check_bound(kTest, exact, magnitude, 8, 1, weight_gradient.at(row, column), record);
            }
        }
    }
    ::fixy::check::pass(kTest,
                        "test_mimic_cpu_kernels: {} outputs of the products of the first model have the bits of "
                        "the tree and are inside the bound, the largest error is {} of the bound\n",
                        record.outputs, record.largest_ratio);
}

// A block of rows of a product with a bias, which one thread runs under its
// own scope and context.
template <class Chunk>
struct RowBlock {
    Chunk chunk;
    std::size_t first_row = 0;
    std::size_t rows = 0;
    std::size_t columns = 0;
    ::fixy::StridedView<float const, 2> left;
    ::fixy::StridedView<float const, 2> right;
    ::fixy::StridedView<float const, 2> bias;

    void operator()() noexcept {
        ::fixy::FixedArray<std::size_t, 2> const sizes{rows, columns};
        ::fixy::FixedArray<std::size_t, 2> const strides{columns, 1};
        auto target = ::fixy::mint_strided_view_mut(std::move(chunk), sizes.view(), strides.view(), 0)
                          .expect("a block fits its chunk");
        auto const left_rows = left.slice(::fixy::index<0>(), first_row, rows).expect("the block is inside the left");
        auto const bias_rows = bias.slice(::fixy::index<0>(), first_row, rows).expect("the block is inside the bias");
        with_kernel_context([&](KernelCtx const& ctx) noexcept {
            ::fixy::run_kernel(
                ctx, cpu::multiply_add_bias, target, ::fixy::side_as<::fixy::side::LeftFactor>(left_rows),
                ::fixy::side_as<::fixy::side::RightFactor>(right), ::fixy::side_as<cpu::side::Bias>(bias_rows));
        });
    }
};

// The rows of a product of 29 rows, split into Threads blocks, each on a
// thread of its own.
template <std::size_t Threads>
[[nodiscard]] ::fixy::HeapArray<float> multiply_on_threads(Matrix<float> const& left, Matrix<float> const& right,
                                                           Matrix<float> const& bias) noexcept {
    std::size_t const rows = left.shape.rows;
    std::size_t const columns = right.shape.columns;
    std::size_t const block = (rows + Threads - 1) / Threads;
    ::fixy::HeapArray<float> out =
        ::fixy::mint_heap_array(::crucible::test::rig_alloc(), ::fixy::strict_mul(rows, columns), 0.0f);
    out = ::fixy::with_mut(std::move(out), [&](auto run) noexcept {
        auto chunks = ::fixy::split_chunks(std::move(run), block * columns);
        using Chunk = ::fixy::ViewMut<float, typename decltype(run)::brand_type>;
        auto const make_block = [&](std::size_t index) noexcept {
            auto chunk = chunks.next().expect("one chunk for each thread");
            std::size_t const count = chunk.size() / columns;
            return RowBlock<Chunk>{std::move(chunk),         index * block, count, columns, left.view(), right.view(),
                                   broadcast_row(bias, rows)};
        };
        [&]<std::size_t... Indexes>(std::index_sequence<Indexes...>) noexcept {
            // A braced list makes the blocks in the order of the chunks, and each
            // block then moves into the frame of the door.
            ::fixy::Record<decltype(make_block(Indexes))...> blocks{make_block(Indexes)...};
            ::fixy::BgLoadCtx const bg{eff::testing::bg()};
            bool const is_run = ::fixy::thread::run_joined(bg, ::fixy::thread::ThreadOptions{},
                                                           std::move(blocks.at(::fixy::index<Indexes>()))...)
                                    .is_ok();
            CRUCIBLE_TEST_ASSERT(kTest, is_run);
        }(std::make_index_sequence<Threads>{});
    });
    return out;
}

// A split of the rows over 1, 2, 3 and 4 threads gives the bits of one call.
void test_the_threads_change_no_bit() {
    PhiloxStream stream{0x6E1C000000000011u};
    Matrix<float> left = make_matrix<float>(MatrixShape{29, 40}, Layout::RowMajor, 0);
    Matrix<float> right = make_matrix<float>(MatrixShape{40, 33}, Layout::ColumnMajor, 0);
    Matrix<float> bias = make_matrix<float>(MatrixShape{1, 33}, Layout::RowMajor, 0);
    ::crucible::test::fill_matrix(left, stream, 20);
    ::crucible::test::fill_matrix(right, stream, 20);
    ::crucible::test::fill_matrix(bias, stream, 20);
    Matrix<float> whole = make_matrix<float>(MatrixShape{29, 33}, Layout::RowMajor, 0);
    with_kernel_context([&](KernelCtx const& ctx) noexcept {
        run_into(ctx, whole, cpu::multiply_add_bias, ::fixy::side_as<::fixy::side::LeftFactor>(left.view()),
                 ::fixy::side_as<::fixy::side::RightFactor>(right.view()),
                 ::fixy::side_as<cpu::side::Bias>(broadcast_row(bias, 29)));
    });
    auto const check = [&](::fixy::HeapArray<float> const& parts) noexcept {
        for (std::size_t position = 0; position < 29 * 33; ++position) {
            CRUCIBLE_TEST_ASSERT(kTest, same_bits(*parts.view().get(position).expect(
                                                      "a test reads an element at or past the count of its View"),
                                                  *whole.values.view().get(position).expect(
                                                      "a test reads an element at or past the count of its View")));
        }
    };
    check(multiply_on_threads<1>(left, right, bias));
    check(multiply_on_threads<2>(left, right, bias));
    check(multiply_on_threads<3>(left, right, bias));
    check(multiply_on_threads<4>(left, right, bias));
    ::fixy::check::pass(kTest,
                        "test_mimic_cpu_kernels: a product split over 1, 2, 3 and 4 threads gives the bits of one "
                        "thread\n");
}

// The selects, the fill and the fused add give their exact bits.
void test_the_exact_element_kernels() {
    PhiloxStream stream{0x6E1C000000000012u};
    MatrixShape const shape{8, 24};
    Matrix<float> const values = drawn_matrix(stream, shape, Layout::RowMajor);
    Matrix<float> const gates = drawn_matrix(stream, shape, Layout::PaddedRows);
    Matrix<float> const others = drawn_matrix(stream, shape, Layout::ColumnMajor);
    Matrix<float> clamped = make_matrix<float>(shape, Layout::RowMajor, 0);
    Matrix<float> gated = make_matrix<float>(shape, Layout::Spread, 0);
    Matrix<float> filled = make_matrix<float>(shape, Layout::RowMajor, 0);
    Matrix<float> updated = make_matrix<float>(shape, Layout::RowMajor, 1);
    ::crucible::test::edit_matrix(updated, [&values](std::size_t row, std::size_t column, float& value) noexcept {
        value = values.at(row, column);
    });
    float const fill_value = ::fixy::bit_cast<float>(std::uint32_t{0x7FA12345u});
    float const scale = -0.01f;
    with_kernel_context([&](KernelCtx const& ctx) noexcept {
        run_into(ctx, clamped, cpu::clamp_below_at_zero, values.view());
        run_into(ctx, gated, cpu::zero_at_or_below_threshold, ::fixy::side_as<cpu::side::Value>(values.view()),
                 ::fixy::side_as<cpu::side::Gate>(gates.view()), ::fixy::side_as<cpu::side::Threshold>(0.0f));
        run_into(ctx, filled, cpu::fill, ::fixy::side_as<cpu::side::FillValue>(fill_value));
        run_into(ctx, updated, cpu::add_scaled_in_place, ::fixy::side_as<cpu::side::Scale>(scale),
                 ::fixy::side_as<cpu::side::Other>(others.view()));
    });
    for (std::size_t row = 0; row < 8; ++row) {
        for (std::size_t column = 0; column < 24; ++column) {
            float const value = values.at(row, column);
            CRUCIBLE_TEST_ASSERT(kTest, same_bits(clamped.at(row, column), value < 0.0f ? 0.0f : value));
            CRUCIBLE_TEST_ASSERT(kTest, same_bits(gated.at(row, column), gates.at(row, column) <= 0.0f ? 0.0f : value));
            CRUCIBLE_TEST_ASSERT(kTest, same_bits(filled.at(row, column), fill_value));
            float const fused = ::crucible::test::exact_fma(scale, others.at(row, column), value);
            CRUCIBLE_TEST_ASSERT(kTest, same_bits(updated.at(row, column), ::crucible::test::canonical_output(fused)));
        }
    }
    ::fixy::check::pass(kTest, "test_mimic_cpu_kernels: the clamp, the gate, the fill and the scaled add give their "
                               "stated bits\n");
}

// The square of a difference, the quotient and the scaled product of a
// difference are inside their bounds.
void test_the_rounded_element_kernels() {
    PhiloxStream stream{0x6E1C000000000013u};
    MatrixShape const shape{8, 24};
    Matrix<float> const lefts = drawn_matrix(stream, shape, Layout::RowMajor);
    Matrix<float> const rights = drawn_matrix(stream, shape, Layout::Spread);
    Matrix<float> factor = make_matrix<float>(MatrixShape{1, 24}, Layout::RowMajor, 0);
    ::crucible::test::fill_matrix(factor, stream, 10);
    Matrix<float> squares = make_matrix<float>(shape, Layout::RowMajor, 0);
    Matrix<float> scaled = make_matrix<float>(shape, Layout::ColumnMajor, 0);
    Matrix<float> quotients = make_matrix<float>(shape, Layout::RowMajor, 0);
    ::crucible::test::edit_matrix(quotients, [&lefts](std::size_t row, std::size_t column, float& value) noexcept {
        value = lefts.at(row, column);
    });
    float const scale = 2.0f / 192.0f;
    float const denominator = 3.0f;
    with_kernel_context([&](KernelCtx const& ctx) noexcept {
        run_into(ctx, squares, cpu::square_difference, ::fixy::side_as<cpu::side::Minuend>(lefts.view()),
                 ::fixy::side_as<cpu::side::Subtrahend>(rights.view()));
        ::crucible::test::run_scale_product_of_difference<KernelUnit::Build>(
            ctx, scaled, ::fixy::side_as<cpu::side::Minuend>(lefts.view()),
            ::fixy::side_as<cpu::side::Subtrahend>(rights.view()), ::fixy::side_as<cpu::side::Scale>(scale),
            ::fixy::side_as<cpu::side::Factor>(broadcast_row(factor, 8)));
        run_into(ctx, quotients, cpu::divide_in_place, ::fixy::side_as<cpu::side::Denominator>(denominator));
    });
    BoundRecord record{};
    for (std::size_t row = 0; row < 8; ++row) {
        for (std::size_t column = 0; column < 24; ++column) {
            float const left = lefts.at(row, column);
            float const right = rights.at(row, column);
            double const wide_quotient = static_cast<double>(left) / static_cast<double>(denominator);
            CRUCIBLE_TEST_ASSERT(kTest, same_bits(quotients.at(row, column), ::crucible::test::canonical_output(
                                                                                 static_cast<float>(wide_quotient))));
            if (!::fixy::fp::is_finite(left) || !::fixy::fp::is_finite(right)) {
                CRUCIBLE_TEST_ASSERT(kTest, !::fixy::fp::is_finite(squares.at(row, column)));
                continue;
            }
            ExactSum<float> exact{};
            exact.add_product(left, left);
            exact.add_product(-left, right);
            exact.add_product(-left, right);
            exact.add_product(right, right);
            ::crucible::test::check_bound(kTest, exact, exact, 1, 3, squares.at(row, column), record);
            double const factor_value = factor.at(0, column);
            double const expected =
                static_cast<double>(scale) * (static_cast<double>(left) - static_cast<double>(right)) * factor_value;
            double const bound = ::crucible::test::gamma_of<float>(3) * ::fixy::fp::abs(expected)
                               + 0x1p-150 * (1.0 + 0x1p-24) * ::fixy::fp::abs(factor_value) + 0x1p-150
                               + 0x1p-51 * ::fixy::fp::abs(expected);
            CRUCIBLE_TEST_ASSERT(kTest,
                                 ::fixy::fp::abs(static_cast<double>(scaled.at(row, column)) - expected) <= bound);
        }
    }
    ::fixy::check::pass(kTest,
                        "test_mimic_cpu_kernels: {} squares of a difference are inside the bound, the largest "
                        "error is {} of the bound, and the quotients and the scaled products agree\n",
                        record.outputs, record.largest_ratio);
}

// The sums against the model of the tree and inside the bound, and the mean
// of the squared differences of the loss inside its composite bound:
// gamma(count_tree_depth(n) + 4) of the exact mean.
void test_the_sums() {
    PhiloxStream stream{0x6E1C000000000014u};
    BoundRecord record{};
    for (std::size_t round = 0; round < 50; ++round) {
        std::size_t const rows = 1 + stream.below(40);
        std::size_t const columns = 1 + stream.below(30);
        Matrix<float> input = make_matrix<float>(MatrixShape{rows, columns}, Layout::RowMajor, 0);
        Matrix<float> target = make_matrix<float>(MatrixShape{rows, columns}, Layout::RowMajor, 0);
        ::crucible::test::fill_matrix(input, stream, 20);
        ::crucible::test::fill_matrix(target, stream, 20);
        Matrix<float> column_sums = make_matrix<float>(MatrixShape{1, columns}, Layout::Spread, 0);
        Matrix<float> total = make_matrix<float>(MatrixShape{1, 1}, Layout::RowMajor, 0);
        Matrix<float> squares = make_matrix<float>(MatrixShape{rows, columns}, Layout::RowMajor, 0);
        Matrix<float> loss = make_matrix<float>(MatrixShape{1, 1}, Layout::RowMajor, 0);
        std::size_t const count = rows * columns;
        with_kernel_context([&](KernelCtx const& ctx) noexcept {
            run_into_row(ctx, column_sums, cpu::sum_leading, input.view());
            run_into(ctx, total, cpu::sum_all, input.view());
            run_into(ctx, squares, cpu::square_difference, ::fixy::side_as<cpu::side::Minuend>(input.view()),
                     ::fixy::side_as<cpu::side::Subtrahend>(target.view()));
            run_into(ctx, loss, cpu::sum_all, squares.view());
            run_into(ctx, loss, cpu::divide_in_place,
                     ::fixy::side_as<cpu::side::Denominator>(static_cast<float>(count)));
        });
        for (std::size_t column = 0; column < columns; ++column) {
            CRUCIBLE_TEST_ASSERT(kTest,
                                 same_bits(column_sums.at(0, column), ::crucible::test::model_runs(input, column)));
            ExactSum<float> exact{};
            ExactSum<float> magnitude{};
            for (std::size_t row = 0; row < rows; ++row) {
                exact.add(input.at(row, column));
                magnitude.add(::fixy::fp::abs(input.at(row, column)));
            }
            ::crucible::test::check_bound(kTest, exact, magnitude, rows, 0, column_sums.at(0, column), record);
        }
        auto const leaf = [&](std::size_t index) noexcept { return input.at(index / columns, index % columns); };
        CRUCIBLE_TEST_ASSERT(
            kTest, same_bits(total.at(0, 0),
                             ::crucible::test::canonical_output(::crucible::test::model_tree<float>(leaf, 0, count))));
        ExactSum<float> squared{};
        for (std::size_t row = 0; row < rows; ++row) {
            for (std::size_t column = 0; column < columns; ++column) {
                float const left = input.at(row, column);
                float const right = target.at(row, column);
                squared.add_product(left, left);
                squared.add_product(-left, right);
                squared.add_product(-left, right);
                squared.add_product(right, right);
            }
        }
        double const mean = squared.rounded_as<double>() / static_cast<double>(count);
        double const loss_bound =
            ::crucible::test::gamma_of<float>(::crucible::test::depth_of(count) + 4) * mean + 0x1p-50 * mean + 0x1p-140;
        CRUCIBLE_TEST_ASSERT(kTest, ::fixy::fp::abs(static_cast<double>(loss.at(0, 0)) - mean) <= loss_bound);
    }
    ::fixy::check::pass(kTest,
                        "test_mimic_cpu_kernels: the sums have the bits of the tree, {} outputs are inside the "
                        "bound, the largest error is {} of the bound, and each mean of the squared differences is "
                        "inside its bound\n",
                        record.outputs, record.largest_ratio);
}

// The digests of the fixed draws, which each build gives.  The kernels fix
// each bit, so a change of a digest is a change of a stated order or of a
// draw.  Take a new digest only from two units of different vector widths
// that agree, and give the reason in the change.
inline constexpr KernelDigests kDigests{9627877481138780042u, 12828840277690363947u, 4733828253670995231u};

[[nodiscard]] bool agree_with_the_constants(KernelDigests digests) noexcept {
    bool const is_same =
        digests.products == kDigests.products && digests.elements == kDigests.elements && digests.sums == kDigests.sums;
    if (!is_same) {
        ::fixy::report(::fixy::Sink::Err, "test_mimic_cpu_kernels: the digests are {}, {} and {}\n", digests.products,
                       digests.elements, digests.sums);
    }
    return is_same;
}

void test_the_digests_hold() {
    CRUCIBLE_TEST_ASSERT(kTest,
                         agree_with_the_constants(::crucible::test::kernel_digests_of_this_unit<KernelUnit::Build>()));
    std::size_t units = 1;
#if defined(__x86_64__)
    namespace os = ::foundation::os;
    // The build compiles the units of each level other than the level of the
    // build (test/CMakeLists.txt).
    constexpr bool has_v3_unit = CRUCIBLE_TEST_HAS_V3_UNIT != 0;
    constexpr bool has_v4_unit = CRUCIBLE_TEST_HAS_V4_UNIT != 0;
    os::X86Level const host = os::x86_level_of(os::read_x86_feature_words());
    if constexpr (has_v3_unit) {
        if (host >= os::X86Level::v3) {
            CRUCIBLE_TEST_ASSERT(kTest, agree_with_the_constants(::crucible::test::kernel_digests_of_v3_unit()));
            ++units;
        }
    }
    if constexpr (has_v4_unit) {
        if (host >= os::X86Level::v4) {
            CRUCIBLE_TEST_ASSERT(kTest, agree_with_the_constants(::crucible::test::kernel_digests_of_v4_unit()));
            ++units;
        }
    }
#endif
    ::fixy::check::pass(kTest, "test_mimic_cpu_kernels: the digests of the kernels hold in {} units\n", units);
}

}  // namespace

int main() {
    test_the_products_of_the_first_model();
    test_the_threads_change_no_bit();
    test_the_exact_element_kernels();
    test_the_rounded_element_kernels();
    test_the_sums();
    test_the_digests_hold();
    return 0;
}
