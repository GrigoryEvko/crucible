#pragma once

// The rig of test_core_elementwise: the drawn matrices, the run of an op
// through write_pointwise on two layouts, and the tests of the selects, of
// the single rounds and of the square of a difference.
//
// The compile of an op through write_pointwise gives one lane plan for each
// mix of the loops of its inputs, with three loops in place of four for each
// input past the second, so each op costs much machine code.  Thus the tests
// compile in more than one unit.  Each unit elementwise_unit_*.cpp of
// test_core_elementwise gives one or two of the functions at the end of this
// header, and each such function runs a test of this header for one element
// type: the selects and the single rounds for each element type in a unit of
// its own, and the squares of a difference in one unit.  The unit
// test_core_elementwise.cpp holds main and the test of the scaled product.

#include <fixy/Core.h>
#include <fixy/Elementwise.h>

#include "../exact_sum.h"
#include "../product_rig.h"
#include "../test_assert.h"
#include "philox_stream.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace crucible::test {

inline constexpr std::size_t kRows = 13;
inline constexpr std::size_t kColumns = 37;

// A drawn value: about one in eight is a hard case.
template <class T>
[[nodiscard]] T draw_hard_value(PhiloxStream& stream) noexcept {
    std::uint64_t const kind = stream.below(16);
    switch (kind) {
        case 0:
            return T{0};
        case 1:
            return -T{0};
        case 2:
            return ::fixy::min_subnormal_of<T> * static_cast<T>(1 + stream.below(1000));
        case 3:
            return (stream.below(2) == 0 ? T{1} : -T{1}) * ::fixy::infinity_of<T>;
        case 4: {
            if constexpr (sizeof(T) == sizeof(float)) {
                return ::fixy::bit_cast<float>(0xFFC00000u | (stream.next_word() & 0x3FFFFFu));
            } else {
                return ::fixy::bit_cast<double>(0x7FF4000000000000u | (stream.next_wide() & 0x3FFFFFFFFFFFu));
            }
        }
        default:
            return ::crucible::test::draw_spread_value<T>(stream, 30);
    }
}

// A matrix of drawn values in the layout.
template <class T>
[[nodiscard]] Matrix<T> drawn_matrix(PhiloxStream& stream, Layout layout) noexcept {
    Matrix<T> matrix = make_matrix<T>(MatrixShape{kRows, kColumns}, layout, 1);
    for (std::size_t row = 0; row < kRows; ++row) {
        for (std::size_t column = 0; column < kColumns; ++column) {
            element_at(matrix.values.view(), matrix.position(row, column)) = draw_hard_value<T>(stream);
        }
    }
    return matrix;
}

// The copy of a matrix in another layout.
template <class T>
[[nodiscard]] Matrix<T> relaid(Matrix<T> const& source, Layout layout) noexcept {
    Matrix<T> matrix = make_matrix<T>(source.shape, layout, 2);
    for (std::size_t row = 0; row < kRows; ++row) {
        for (std::size_t column = 0; column < kColumns; ++column) {
            element_at(matrix.values.view(), matrix.position(row, column)) = source.at(row, column);
        }
    }
    return matrix;
}

// The output of op over the inputs, in the layout.
template <class T, class Op, class... Inputs>
[[nodiscard]] Matrix<T> run_op(Op op, Layout layout, Inputs const&... inputs) noexcept {
    Matrix<T> out = make_matrix<T>(MatrixShape{kRows, kColumns}, layout, 0);
    ::fixy::FixedArray<std::size_t, 2> const sizes{kRows, kColumns};
    ::fixy::FixedArray<std::size_t, 2> const strides{out.place.row_stride, out.place.column_stride};
    std::size_t const offset = out.place.offset;
    auto [result, back] = ::fixy::with_mut(std::move(out.values), [&](auto run) noexcept {
        auto target = ::fixy::mint_strided_view_mut(std::move(run), sizes.view(), strides.view(), offset)
                          .expect("the output fits its array");
        return ::fixy::write_pointwise(target, op, inputs.view()...).is_ok();
    });
    out.values = std::move(back);
    assert(result);
    return out;
}

// The output of op on the dense layout and on the spread layout, which must
// have the same bits.
template <class T, class Op, class... Inputs>
[[nodiscard]] Matrix<T> run_on_two_layouts(Op op, Inputs const&... inputs) noexcept {
    Matrix<T> dense = run_op<T>(op, Layout::RowMajor, relaid(inputs, Layout::RowMajor)...);
    Matrix<T> spread = run_op<T>(op, Layout::Spread, relaid(inputs, Layout::Spread)...);
    for (std::size_t row = 0; row < kRows; ++row) {
        for (std::size_t column = 0; column < kColumns; ++column) {
            assert(bits_of_float(dense.at(row, column)) == bits_of_float(spread.at(row, column)));
        }
    }
    return dense;
}

template <class T>
[[nodiscard]] bool same_bits(T left, T right) noexcept {
    return bits_of_float(left) == bits_of_float(right);
}

// The selects and the copy give their stated values, and a NaN keeps its
// bits.
template <class T>
void test_the_selects_keep_their_bits() {
    PhiloxStream stream{0xE1E0000000000001u + sizeof(T)};
    Matrix<T> const values = drawn_matrix<T>(stream, Layout::RowMajor);
    Matrix<T> const gates = drawn_matrix<T>(stream, Layout::RowMajor);
    Matrix<T> const thresholds = drawn_matrix<T>(stream, Layout::RowMajor);
    Matrix<T> const clamped = run_on_two_layouts<T>(::fixy::elementwise::ClampBelowAtZero{}, values);
    Matrix<T> const gated =
        run_on_two_layouts<T>(::fixy::elementwise::ZeroAtOrBelowThreshold{}, values, gates, thresholds);
    Matrix<T> const copied = run_on_two_layouts<T>(::fixy::elementwise::Copy{}, values);
    for (std::size_t row = 0; row < kRows; ++row) {
        for (std::size_t column = 0; column < kColumns; ++column) {
            T const value = values.at(row, column);
            T const gate = gates.at(row, column);
            T const threshold = thresholds.at(row, column);
            T const clamp = value < T{0} ? T{0} : value;
            T const gate_result = gate <= threshold ? T{0} : value;
            assert(same_bits(clamped.at(row, column), clamp));
            assert(same_bits(gated.at(row, column), gate_result));
            assert(same_bits(copied.at(row, column), value));
        }
    }
    ::crucible::test::pass("test_core_elementwise: the selects and the copy of {} bytes keep their bits\n", sizeof(T));
}

// The IEEE result of a sum and of a quotient of two floats: the value in
// double rounded to a float.
[[nodiscard]] inline float float_result(double value) noexcept { return static_cast<float>(value); }

// The canonical NaN for a NaN.
template <class T>
[[nodiscard]] T canonical(T value) noexcept {
    return ::crucible::test::canonical_output(value);
}

// The copy of a matrix with 3 in place of each zero, so that no oracle step
// divides by zero: the full UBSan of the gate refuses a division of floats
// by zero.
template <class T>
[[nodiscard]] Matrix<T> without_zeros(Matrix<T> const& source) noexcept {
    Matrix<T> matrix = relaid(source, Layout::RowMajor);
    for (std::size_t row = 0; row < kRows; ++row) {
        for (std::size_t column = 0; column < kColumns; ++column) {
            T const value = source.at(row, column);
            if (::fixy::fp::abs(value) < ::fixy::min_subnormal_of<T>) {
                element_at(matrix.values.view(), matrix.position(row, column)) = T{3};
            }
        }
    }
    return matrix;
}

// Sum and Quotient give the IEEE result, and FusedMultiplyAdd gives the
// exact fma.
template <class T>
void test_the_single_rounds_are_exact() {
    PhiloxStream stream{0xE1E0000000000002u + sizeof(T)};
    Matrix<T> const lefts = drawn_matrix<T>(stream, Layout::RowMajor);
    Matrix<T> const rights = drawn_matrix<T>(stream, Layout::RowMajor);
    Matrix<T> const addends = drawn_matrix<T>(stream, Layout::RowMajor);
    Matrix<T> const denominators = without_zeros(rights);
    Matrix<T> const sums = run_on_two_layouts<T>(::fixy::elementwise::Sum{}, lefts, rights);
    Matrix<T> const quotients = run_on_two_layouts<T>(::fixy::elementwise::Quotient{}, lefts, denominators);
    Matrix<T> const fused = run_on_two_layouts<T>(::fixy::elementwise::FusedMultiplyAdd{}, lefts, rights, addends);
    std::size_t checked = 0;
    for (std::size_t row = 0; row < kRows; ++row) {
        for (std::size_t column = 0; column < kColumns; ++column) {
            T const left = lefts.at(row, column);
            T const right = rights.at(row, column);
            T const addend = addends.at(row, column);
            assert(same_bits(fused.at(row, column), canonical(::crucible::test::exact_fma(left, right, addend))));
            if constexpr (sizeof(T) == sizeof(float)) {
                double const wide_left = left;
                double const wide_right = right;
                double const wide_denominator = denominators.at(row, column);
                assert(same_bits(sums.at(row, column), canonical(float_result(wide_left + wide_right))));
                assert(same_bits(quotients.at(row, column), canonical(float_result(wide_left / wide_denominator))));
            } else {
                ExactSum<T> exact{};
                exact.add(left);
                exact.add(right);
                T const rounded = exact.template rounded_as<T>();
                // An exact sum of zero is +0, and -0 + -0 is -0.
                bool const is_negative_zero_sum = ::fixy::fp::sign_bit(left) && ::fixy::fp::sign_bit(right)
                                               && ::fixy::fp::abs(left) < ::fixy::min_subnormal_of<T>
                                               && ::fixy::fp::abs(right) < ::fixy::min_subnormal_of<T>;
                assert(same_bits(sums.at(row, column), is_negative_zero_sum ? -T{0} : rounded));
            }
            ++checked;
        }
    }
    ::crucible::test::pass("test_core_elementwise: {} sums, quotients and fused products of {} bytes give the "
                           "IEEE result\n",
                           checked, sizeof(T));
}

// SquareOfDifference is inside gamma(3) * y + eta of the exact square.
template <class T>
void test_the_square_of_a_difference_is_inside_its_bound() {
    PhiloxStream stream{0xE1E0000000000003u + sizeof(T)};
    Matrix<T> const lefts = drawn_matrix<T>(stream, Layout::RowMajor);
    Matrix<T> rights = drawn_matrix<T>(stream, Layout::RowMajor);
    // Each fourth right is near its left, so the difference cancels.
    for (std::size_t row = 0; row < kRows; row += 4) {
        for (std::size_t column = 0; column < kColumns; ++column) {
            T const left = lefts.at(row, column);
            element_at(rights.values.view(), rights.position(row, column)) =
                left * (T{1} + ::fixy::epsilon_of<T> * static_cast<T>(stream.below(5)));
        }
    }
    Matrix<T> const squares = run_on_two_layouts<T>(::fixy::elementwise::SquareOfDifference{}, lefts, rights);
    ::crucible::test::BoundRecord record{};
    for (std::size_t row = 0; row < kRows; ++row) {
        for (std::size_t column = 0; column < kColumns; ++column) {
            T const left = lefts.at(row, column);
            T const right = rights.at(row, column);
            T const square = squares.at(row, column);
            if (!::fixy::fp::is_finite(left) || !::fixy::fp::is_finite(right)) {
                assert(::fixy::fp::is_nan(square) ? same_bits(square, ::fixy::quiet_nan_of<T>)
                                                  : same_bits(square, ::fixy::infinity_of<T>));
                continue;
            }
            // The exact square: a^2 - 2ab + b^2, each product exact.
            ExactSum<T> exact{};
            exact.add_product(left, left);
            exact.add_product(-left, right);
            exact.add_product(-left, right);
            exact.add_product(right, right);
            double const exact_value = exact.template rounded_as<double>();
            if (!::fixy::fp::is_finite(exact_value) || exact_value > static_cast<double>(::fixy::max_of<T>)) continue;
            // One leaf of three rounds: the bound gamma(3) * y + 3 * eta, which
            // holds when gamma(3) * y + eta holds.
            ExactSum<T> const magnitude = exact;
            ::crucible::test::check_bound(exact, magnitude, 1, 3, square, record);
        }
    }
    ::crucible::test::pass("test_core_elementwise: {} squares of a difference of {} bytes are inside the bound, the "
                           "largest error is {} of the bound\n",
                           record.outputs, sizeof(T), record.largest_ratio);
}

// The tests of the units, each for one element type.
void test_the_float_selects_keep_their_bits();  // elementwise_unit_selects_float.cpp
void test_the_double_selects_keep_their_bits();  // elementwise_unit_selects_double.cpp
void test_the_float_single_rounds_are_exact();  // elementwise_unit_rounds_float.cpp
void test_the_double_single_rounds_are_exact();  // elementwise_unit_rounds_double.cpp
void test_the_float_square_of_a_difference_is_inside_its_bound();  // elementwise_unit_squares.cpp
void test_the_double_square_of_a_difference_is_inside_its_bound();  // elementwise_unit_squares.cpp

}  // namespace crucible::test
