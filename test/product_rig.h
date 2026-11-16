#pragma once

// The rig of the tests of the tree products (foundation/core/Product.h) and
// of the kernels of the CPU backend: matrices of drawn values in drawn
// layouts, the scalar model of the tree, and the check of the error bound
// against the exact oracle of test/exact_sum.h.
//
// A Matrix holds its elements in a HeapArray, with an offset and a stride for
// each of its two axes.  The model reads each element through its position in
// the array, and it shares no code with the code under test.

#include <fixy/Core.h>
#include <foundation/effects/Effect.h>

#include "exact_sum.h"
#include "foundation/philox_stream.h"
#include "test_assert.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace crucible::test {

[[nodiscard]] inline ::foundation::effects::Alloc rig_alloc() noexcept {
    return ::foundation::effects::testing::test().alloc;
}

// The layout of a matrix in its array.
enum class Layout : std::uint8_t {
    // The last axis is consecutive.
    RowMajor = 0,
    // The first axis is consecutive.
    ColumnMajor = 1,
    // Each row starts three elements after the end of the row before it.
    PaddedRows = 2,
    // Each element of a row is two elements after the one before it, and the
    // rows are apart.
    Spread = 3,
};

inline constexpr std::size_t kLayouts = 4;

// The sizes of a matrix.
struct MatrixShape {
    std::size_t rows = 0;
    std::size_t columns = 0;
};

// The place of each element of a matrix in its array.
struct MatrixPlace {
    std::size_t offset = 0;
    std::size_t row_stride = 0;
    std::size_t column_stride = 0;
    std::size_t extent = 0;
};

[[nodiscard]] inline MatrixPlace place_of(MatrixShape shape, Layout layout, std::size_t offset) noexcept {
    MatrixPlace place{};
    place.offset = offset;
    switch (layout) {
        case Layout::RowMajor:
            place.row_stride = shape.columns;
            place.column_stride = 1;
            break;
        case Layout::ColumnMajor:
            place.row_stride = 1;
            place.column_stride = shape.rows;
            break;
        case Layout::PaddedRows:
            place.row_stride = shape.columns + 3;
            place.column_stride = 1;
            break;
        case Layout::Spread:
            place.row_stride = 2 * shape.columns + 5;
            place.column_stride = 2;
            break;
        default:
            ::fixy::unreachable();
    }
    if (shape.rows == 0 || shape.columns == 0) {
        place.extent = offset + 1;
    } else {
        place.extent = offset + (shape.rows - 1) * place.row_stride + (shape.columns - 1) * place.column_stride + 1;
    }
    return place;
}

// A matrix of T in an array of its own.
template <class T>
struct Matrix {
    MatrixShape shape{};
    MatrixPlace place{};
    ::fixy::HeapArray<T> values;

    [[nodiscard]] std::size_t position(std::size_t row, std::size_t column) const noexcept {
        return place.offset + row * place.row_stride + column * place.column_stride;
    }
    [[nodiscard]] T at(std::size_t row, std::size_t column) const noexcept {
        return element_at(values.view(), position(row, column));
    }

    // The strided view of the elements.
    [[nodiscard]] ::fixy::StridedView<T const, 2> view() const noexcept {
        ::fixy::FixedArray<std::size_t, 2> const sizes{shape.rows, shape.columns};
        ::fixy::FixedArray<std::size_t, 2> const strides{place.row_stride, place.column_stride};
        return ::fixy::mint_strided_view(::fixy::View<T const>{values.view()}, sizes.view(), strides.view(),
                                         place.offset)
            .expect("a matrix fits its array");
    }
};

// A matrix of the shape and the layout, with each element 0 and each slot of
// the array outside the matrix a NaN of a payload that no output has.
template <class T>
[[nodiscard]] Matrix<T> make_matrix(MatrixShape shape, Layout layout, std::size_t offset) noexcept {
    MatrixPlace const place = place_of(shape, layout, offset);
    Matrix<T> matrix{shape, place, ::fixy::mint_heap_array(rig_alloc(), place.extent, T{})};
    for (T& value : matrix.values) {
        value = -::fixy::quiet_nan_of<T>;
    }
    for (std::size_t row = 0; row < shape.rows; ++row) {
        for (std::size_t column = 0; column < shape.columns; ++column) {
            element_at(matrix.values.view(), matrix.position(row, column)) = T{};
        }
    }
    return matrix;
}

// A value with a random sign, a random fraction and an exponent in [-spread,
// spread], so each association of a sum changes its bits.
template <class T>
[[nodiscard]] T draw_spread_value(PhiloxStream& stream, std::uint64_t spread) noexcept {
    std::uint64_t const bits = stream.next_wide();
    if constexpr (sizeof(T) == sizeof(float)) {
        auto const exponent = static_cast<std::uint32_t>(127 + stream.below(2 * spread + 1) - spread);
        return ::fixy::bit_cast<float>(static_cast<std::uint32_t>(((bits >> 40) & 1u) << 31) | (exponent << 23)
                                       | static_cast<std::uint32_t>(bits & 0x7FFFFFu));
    } else {
        std::uint64_t const exponent = 1023 + stream.below(2 * spread + 1) - spread;
        return ::fixy::bit_cast<double>((((bits >> 63) & 1u) << 63) | (exponent << 52)
                                        | (stream.next_wide() & ((std::uint64_t{1} << 52) - 1)));
    }
}

template <class T>
void fill_matrix(Matrix<T>& matrix, PhiloxStream& stream, std::uint64_t spread) noexcept {
    for (std::size_t row = 0; row < matrix.shape.rows; ++row) {
        for (std::size_t column = 0; column < matrix.shape.columns; ++column) {
            element_at(matrix.values.view(), matrix.position(row, column)) = draw_spread_value<T>(stream, spread);
        }
    }
}

// The bits of a value as an unsigned word.
template <class T>
[[nodiscard]] auto bits_of_float(T value) noexcept {
    return ::fixy::bit_cast<typename ExactLayout<T>::Word>(value);
}

// The output of a sum form: the canonical NaN for a NaN.
template <class T>
[[nodiscard]] T canonical_output(T value) noexcept {
    return ::fixy::fp::is_nan(value) ? ::fixy::quiet_nan_of<T> : value;
}

// The scalar model of the tree of THE ORDER over count leaves: split at the
// largest power of two below the count, found with a loop, and the lower part
// as the left operand.
template <class T, class Leaf>
[[nodiscard]] T model_tree(Leaf const& leaf, std::size_t first, std::size_t count) noexcept {
    if (count == 1) return leaf(first);
    std::size_t split = 1;
    while (split * 2 < count) {
        split *= 2;
    }
    T const left = model_tree<T>(leaf, first, split);
    T const right = model_tree<T>(leaf, first + split, count - split);
    return left + right;
}

// The model of output (row, column) of the product of left and right.
template <class T>
[[nodiscard]] T model_product(Matrix<T> const& left, Matrix<T> const& right, std::size_t row,
                              std::size_t column) noexcept {
    std::size_t const depth = left.shape.columns;
    if (depth == 0) return T{};
    auto const leaf = [&](std::size_t inner) noexcept {
        T const first = left.at(row, inner);
        T const second = right.at(inner, column);
        T const product = first * second;
        return product;
    };
    return canonical_output(model_tree<T>(leaf, 0, depth));
}

// The model of output column of the sum of the runs of runs.
template <class T>
[[nodiscard]] T model_runs(Matrix<T> const& runs, std::size_t column) noexcept {
    if (runs.shape.rows == 0) return T{};
    auto const leaf = [&](std::size_t run) noexcept { return runs.at(run, column); };
    return canonical_output(model_tree<T>(leaf, 0, runs.shape.rows));
}

// The unit roundoff, and a bound of the error of a product that underflows:
// 2^-150 for a float, which is the largest error, and 2^-1074 for a double,
// which is twice the largest error 2^-1075, because a double does not hold
// 2^-1075.
template <class T>
inline constexpr double kUnitRoundoff = sizeof(T) == sizeof(float) ? 0x1p-24 : 0x1p-53;
template <class T>
inline constexpr double kUnderflowError = sizeof(T) == sizeof(float) ? 0x1p-150 : 0x1p-1074;

// gamma(j) = j * u / (1 - j * u) of Higham, in double, with a margin.
template <class T>
[[nodiscard]] double gamma_of(std::size_t count) noexcept {
    double const terms = static_cast<double>(count) * kUnitRoundoff<T>;
    return terms / (1.0 - terms) * (1.0 + 0x1p-40);
}

// The depth of the tree of count leaves: ceil(log2 count).
[[nodiscard]] inline std::size_t depth_of(std::size_t count) noexcept {
    std::size_t depth = 0;
    while ((std::size_t{1} << depth) < count) {
        ++depth;
    }
    return depth;
}

// The record of the checks of the error bound: the count of the outputs, and
// the largest ratio of an error to its bound.
struct BoundRecord {
    std::size_t outputs = 0;
    double largest_ratio = 0.0;
};

// Checks |computed - exact| <= gamma(product_rounds + depth) * sum of the
// absolute terms + the terms of the underflows, for a finite exact sum.  The
// exact error and the sum of the absolute terms are exact sums rounded to a
// double, so each has a relative error of at most 2^-53, which the margin of
// gamma_of covers.  A NaN or an infinite exact sum must give a NaN or an
// infinity of the same sign.
template <class T>
void check_bound(ExactSum<T> const& exact, ExactSum<T> const& magnitude, std::size_t leaves, std::size_t rounds,
                 T computed, BoundRecord& record) noexcept {
    ++record.outputs;
    if (exact.is_nan()) {
        assert(bits_of_float(computed) == bits_of_float(::fixy::quiet_nan_of<T>));
        return;
    }
    double const exact_value = exact.template rounded_as<double>();
    if (!::fixy::fp::is_finite(exact_value)) {
        assert(bits_of_float(static_cast<double>(computed)) == bits_of_float(exact_value));
        return;
    }
    ExactSum<T> error = exact;
    error.add(-computed);
    double const difference = ::fixy::fp::abs(error.template rounded_as<double>());
    std::size_t const depth = leaves == 0 ? 0 : depth_of(leaves);
    double const sum_of_terms = magnitude.template rounded_as<double>();
    double const bound = gamma_of<T>(depth + rounds) * sum_of_terms
                       + (1.0 + gamma_of<T>(depth)) * static_cast<double>(leaves * rounds) * kUnderflowError<T>;
    if (difference > bound) {
        ::fixy::report(::fixy::Sink::Err,
                       "check_bound: {} leaves, the error {} is past the bound {}, the sum of the "
                       "terms is {}\n",
                       leaves, difference, bound, sum_of_terms);
    }
    assert(difference <= bound);
    if (bound > 0.0) {
        double const ratio = difference / bound;
        if (ratio > record.largest_ratio) record.largest_ratio = ratio;
    }
}

// The exact sum and the exact sum of the absolute terms of output (row,
// column) of the product of left and right.
template <class T>
[[nodiscard]] ::fixy::Record<ExactSum<T>, ExactSum<T>> exact_product(Matrix<T> const& left, Matrix<T> const& right,
                                                                     std::size_t row, std::size_t column) noexcept {
    ExactSum<T> exact{};
    ExactSum<T> magnitude{};
    for (std::size_t inner = 0; inner < left.shape.columns; ++inner) {
        T const first = left.at(row, inner);
        T const second = right.at(inner, column);
        exact.add_product(first, second);
        magnitude.add_product(::fixy::fp::abs(first), ::fixy::fp::abs(second));
    }
    return ::fixy::Record<ExactSum<T>, ExactSum<T>>{std::move(exact), std::move(magnitude)};
}

// The FNV-1a digest of 64 bits of the outputs of the forms.
inline constexpr std::uint64_t kDigestOffset = 0xCBF29CE484222325u;
inline constexpr std::uint64_t kDigestPrime = 0x100000001B3u;

template <class T>
[[nodiscard]] std::uint64_t fold_value(std::uint64_t digest, T value) noexcept {
    std::uint64_t const word = bits_of_float(value);
    for (std::size_t position = 0; position < sizeof(T); ++position) {
        digest = (digest ^ ((word >> (8 * position)) & 0xFFu)) * kDigestPrime;
    }
    return digest;
}

}  // namespace crucible::test
