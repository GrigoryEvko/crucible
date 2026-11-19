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
// This unit holds main and the test of the scaled product.  The units that
// elementwise_rig.h names hold the other tests.

#include "elementwise_rig.h"

#include <cstddef>

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
    Matrix<float> const outputs =
        run_on_two_layouts<float>(::fixy::elementwise::ScaledProductOfDifference{}, lefts, rights, scales, factors);
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
            assert(error <= bound);
            if (bound > 0.0 && error / bound > largest_ratio) largest_ratio = error / bound;
            ++checked;
        }
    }
    ::crucible::test::pass("test_core_elementwise: {} scaled products of a difference are inside the bound, the "
                           "largest error is {} of the bound\n",
                           checked, largest_ratio);
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
    return 0;
}
