// Tests of foundation/core/Elementwise.h: the element ops of the pointwise
// walk against an oracle of a higher precision.
//
// Each op runs through write_pointwise on views of two layouts: dense, which
// takes the lane path, and a spread layout, which takes the loads of single
// elements.  The two layouts give the same bits.  The oracle of each output:
//   - the selects and the copy: the stated value, bit for bit
//   - Sum, Quotient: the value in double rounded to a float, which is the
//     IEEE result for float inputs (a sum or a quotient of two floats rounds
//     one time in double and one time to float with no double round error,
//     because 53 >= 2 * 24 + 2), and the exact sum of test/exact_sum.h for a
//     sum of doubles
//   - FusedMultiplyAdd: the exact fma of test/exact_sum.h, bit for bit
//   - SquareOfDifference: the exact square of the difference of
//     test/exact_sum.h, within the bound of the header
//   - ScaledProductOfDifference: the product in double, within the bound of
//     the header plus the error of the double product
// The inputs come from a fixed Philox seed, with the hard cases: the two
// zeros, the subnormal values, the infinities, NaNs with payloads, and
// cancellations.
//
// This unit holds main, the test of the scaled product and the test of each
// mix of the loops of its walk.  The units that elementwise_rig.h names hold
// the other tests.

#include "elementwise_rig.h"

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include <cstddef>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace {

using ::crucible::test::drawn_matrix;
using ::crucible::test::kColumns;
using ::crucible::test::kRows;
using ::crucible::test::Layout;
using ::crucible::test::Matrix;
using ::crucible::test::PhiloxStream;
using ::crucible::test::run_on_two_layouts;

// ScaledProductOfDifference of floats is inside its bound against the
// product in double, whose own error is at most 3 * 2^-53 of the value.
void test_the_scaled_product_is_inside_its_bound() {
    PhiloxStream stream{0xE1E0000000000004u};
    Matrix<float> const lefts = drawn_matrix<float>(stream, Layout::RowMajor);
    Matrix<float> const rights = drawn_matrix<float>(stream, Layout::RowMajor);
    Matrix<float> const scales = drawn_matrix<float>(stream, Layout::RowMajor);
    Matrix<float> const factors = drawn_matrix<float>(stream, Layout::RowMajor);
    Matrix<float> const outputs = run_on_two_layouts<float>(kTest, ::fixy::elementwise::ScaledProductOfDifference{},
                                                            lefts, rights, scales, factors);
    std::size_t checked = 0;
    double largest_ratio = 0.0;
    for (std::size_t row = 0; row < kRows; ++row) {
        for (std::size_t column = 0; column < kColumns; ++column) {
            double const left = lefts.at(row, column);
            double const right = rights.at(row, column);
            double const scale = scales.at(row, column);
            double const factor = factors.at(row, column);
            float const output = outputs.at(row, column);
            double const expected = scale * (left - right) * factor;
            if (!::fixy::fp::is_finite(expected)
                || ::fixy::fp::abs(expected) > static_cast<double>(::fixy::max_of<float>)
                || !::fixy::fp::is_finite(output)) {
                continue;
            }
            double const bound = ::crucible::test::gamma_of<float>(3) * ::fixy::fp::abs(expected)
                               + 0x1p-150 * (1.0 + 0x1p-24) * ::fixy::fp::abs(factor) + 0x1p-150
                               + 0x1p-51 * ::fixy::fp::abs(expected);
            double const error = ::fixy::fp::abs(static_cast<double>(output) - expected);
            CRUCIBLE_TEST_ASSERT(kTest, error <= bound);
            if (bound > 0.0 && error / bound > largest_ratio) largest_ratio = error / bound;
            ++checked;
        }
    }
    ::fixy::check::pass(kTest,
                        "test_core_elementwise: {} scaled products of a difference are inside the bound, the "
                        "largest error is {} of the bound\n",
                        checked, largest_ratio);
}

// The count of the loops of an input of the lane path of
// foundation/core/Pointwise.h: Dense, Splat, StepTwo and General.
constexpr std::size_t kLaneLoops = 4;

// Each mix of the loops of the lane path over the four inputs of the scaled
// product gives the bits of the op on each element: one of the four loops on
// each input, so 256 mixes.  The walk is the walk of the test of the bound
// above, so the mixes add no instance of the lane path.  The last two inputs
// take Dense and Splat in the lane plan, and their steps two and three take
// the loop Switched.  Each input is a matrix in an array of its own that ends
// at its last element, so a read past a view is a read past its allocation.
// A Splat input reads one element for each row, and each row reads a
// different element.  The output is dense, so each mix takes the lane plan,
// with no tile.  O(256) walks.
void test_each_mix_of_the_loops_of_the_scaled_product() {
    PhiloxStream stream{0xE1E0000000000005u};
    ::fixy::FixedArray<std::size_t, kLaneLoops> const steps{1, 0, 2, 3};
    auto const stepped = [&stream](std::size_t step) noexcept {
        ::crucible::test::MatrixPlace place{};
        place.row_stride = step == 0 ? 1 : kColumns * step;
        place.column_stride = step;
        place.extent = (kRows - 1) * place.row_stride + (kColumns - 1) * place.column_stride + 1;
        Matrix<float> matrix{::crucible::test::MatrixShape{kRows, kColumns}, place,
                             ::fixy::mint_heap_array(::crucible::test::rig_alloc(), place.extent, 0.0f)};
        for (float& value : matrix.values) {
            value = ::crucible::test::draw_hard_value<float>(stream);
        }
        return matrix;
    };
    ::fixy::elementwise::ScaledProductOfDifference const op{};
    std::size_t const mixes = kLaneLoops * kLaneLoops * kLaneLoops * kLaneLoops;
    for (std::size_t mix = 0; mix < mixes; ++mix) {
        auto const step_of = [&steps, mix](std::size_t input) noexcept {
            std::size_t loops = mix;
            for (std::size_t skipped = 0; skipped < input; ++skipped) {
                loops /= kLaneLoops;
            }
            return *steps.get(loops % kLaneLoops).expect("a loop below the count of the loops");
        };
        Matrix<float> const lefts = stepped(step_of(0));
        Matrix<float> const rights = stepped(step_of(1));
        Matrix<float> const scales = stepped(step_of(2));
        Matrix<float> const factors = stepped(step_of(3));
        Matrix<float> const outputs =
            ::crucible::test::run_op<float>(kTest, op, Layout::RowMajor, lefts, rights, scales, factors);
        for (std::size_t row = 0; row < kRows; ++row) {
            for (std::size_t column = 0; column < kColumns; ++column) {
                float const expected = ::crucible::test::canonical_output(
                    op(lefts.at(row, column), rights.at(row, column), scales.at(row, column), factors.at(row, column)));
                bool const is_same = ::crucible::test::bits_of_float(outputs.at(row, column))
                                  == ::crucible::test::bits_of_float(expected);
                CRUCIBLE_TEST_ASSERT(kTest, is_same);
            }
        }
    }
    ::fixy::check::pass(kTest,
                        "test_core_elementwise: {} mixes of the loops of the scaled product give the bits of the op "
                        "on each element\n",
                        mixes);
}

}  // namespace

int main() {
    ::crucible::test::test_the_float_selects_keep_their_bits();
    ::crucible::test::test_the_double_selects_keep_their_bits();
    ::crucible::test::test_the_float_single_rounds_are_exact();
    ::crucible::test::test_the_double_single_rounds_are_exact();
    ::crucible::test::test_the_float_square_of_a_difference_is_inside_its_bound();
    ::crucible::test::test_the_double_square_of_a_difference_is_inside_its_bound();
    test_the_scaled_product_is_inside_its_bound();
    test_each_mix_of_the_loops_of_the_scaled_product();
    return 0;
}
