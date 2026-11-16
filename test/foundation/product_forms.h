#pragma once

// The calls of the tree products of foundation/core/Product.h for the tests,
// and the digest of their outputs on fixed draws.
//
// test_core_product and its units of the levels x86-64-v3 and x86-64-v4
// include this header.  Each name is in an unnamed namespace, so each unit
// keeps its own body, compiled for its own target.  The digest is a function
// of the stated order alone, so each unit gives the same digest.

#include <fixy/Core.h>
#include <fixy/Reductions.h>

#include "../product_rig.h"
#include "../test_assert.h"
#include "philox_stream.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace crucible::test {

// The digests of the fixed draws for the two element types.
struct ProductDigests {
    std::uint64_t float_digest = 0;
    std::uint64_t double_digest = 0;
};

namespace {

// The lane counts of a test of the rule that the lanes change no bit.
enum class LaneCount : std::uint8_t {
    Native = 0,
    Two = 2,
    Four = 4,
    Eight = 8,
    Sixteen = 16,
    ThirtyTwo = 32,
};

using ProductResult = ::fixy::Result<::fixy::Unit, ::fixy::ProductError>;

// The product into the unique view target, with the lanes of the count.
template <class T, class Target>
[[nodiscard]] ProductResult multiply_with_lanes(Target& target, ::fixy::LeftFactorOf<T> left,
                                                ::fixy::RightFactorOf<T> right, LaneCount lanes) noexcept {
    switch (lanes) {
        case LaneCount::Native:
            return ::fixy::multiply_by_tree(target, left, right);
        case LaneCount::Two:
            return ::fixy::multiply_by_tree_in_lanes<2>(target, left, right);
        case LaneCount::Four:
            return ::fixy::multiply_by_tree_in_lanes<4>(target, left, right);
        case LaneCount::Eight:
            return ::fixy::multiply_by_tree_in_lanes<8>(target, left, right);
        case LaneCount::Sixteen:
            return ::fixy::multiply_by_tree_in_lanes<16>(target, left, right);
        case LaneCount::ThirtyTwo:
            return ::fixy::multiply_by_tree_in_lanes<32>(target, left, right);
        default:
            ::fixy::unreachable();
    }
}

// The product of left and right into a matrix of the shape and the layout of
// out, with the lanes of the count.  The matrix out keeps its NaNs outside
// the matrix, so a write past an output shows.
template <class T>
[[nodiscard]] ProductResult multiply_into(Matrix<T>& out, ::fixy::StridedView<T const, 2> left,
                                          ::fixy::StridedView<T const, 2> right, LaneCount lanes) noexcept {
    ::fixy::FixedArray<std::size_t, 2> const sizes{out.shape.rows, out.shape.columns};
    ::fixy::FixedArray<std::size_t, 2> const strides{out.place.row_stride, out.place.column_stride};
    std::size_t const offset = out.place.offset;
    auto [result, back] = ::fixy::with_mut(std::move(out.values), [&](auto run) noexcept {
        auto target = ::fixy::mint_strided_view_mut(std::move(run), sizes.view(), strides.view(), offset)
                          .expect("the output fits its array");
        return multiply_with_lanes<T>(target, ::fixy::side_as<::fixy::side::LeftFactor>(left),
                                      ::fixy::side_as<::fixy::side::RightFactor>(right), lanes);
    });
    out.values = std::move(back);
    return result;
}

// The sum of runs into the unique view target, with the lanes of the count.
template <class T, class Target>
[[nodiscard]] ProductResult sum_runs_with_lanes(Target& target, ::fixy::StridedView<T const, 2> runs,
                                                LaneCount lanes) noexcept {
    switch (lanes) {
        case LaneCount::Native:
            return ::fixy::sum_strided_runs_by_tree(target, runs);
        case LaneCount::Two:
            return ::fixy::sum_strided_runs_by_tree_in_lanes<2>(target, runs);
        case LaneCount::Four:
            return ::fixy::sum_strided_runs_by_tree_in_lanes<4>(target, runs);
        case LaneCount::Eight:
            return ::fixy::sum_strided_runs_by_tree_in_lanes<8>(target, runs);
        case LaneCount::Sixteen:
            return ::fixy::sum_strided_runs_by_tree_in_lanes<16>(target, runs);
        case LaneCount::ThirtyTwo:
            return ::fixy::sum_strided_runs_by_tree_in_lanes<32>(target, runs);
        default:
            ::fixy::unreachable();
    }
}

// The sum of the runs of runs into the one row of out.
template <class T>
[[nodiscard]] ProductResult sum_runs_into(Matrix<T>& out, ::fixy::StridedView<T const, 2> runs,
                                          LaneCount lanes) noexcept {
    ::fixy::FixedArray<std::size_t, 1> const sizes{out.shape.columns};
    ::fixy::FixedArray<std::size_t, 1> const strides{out.place.column_stride};
    std::size_t const offset = out.place.offset;
    auto [result, back] = ::fixy::with_mut(std::move(out.values), [&](auto run) noexcept {
        auto target = ::fixy::mint_strided_view_mut(std::move(run), sizes.view(), strides.view(), offset)
                          .expect("the output fits its array");
        return sum_runs_with_lanes<T>(target, runs, lanes);
    });
    out.values = std::move(back);
    return result;
}

// The digest of fixed draws: the products of the three paths in each layout
// of the left factor, the sums of runs and the dot products.
template <class T>
[[nodiscard]] std::uint64_t digest_of_fixed_draws() noexcept {
    PhiloxStream stream{0x9D0C0000000000F0u};
    std::uint64_t digest = kDigestOffset;
    for (std::size_t layout = 0; layout < kLayouts; ++layout) {
        Layout const kind = ::fixy::enum_from<Layout>(static_cast<std::uint8_t>(layout)).expect("a layout");
        Matrix<T> left = make_matrix<T>(MatrixShape{7, 131}, kind, 0);
        Matrix<T> right = make_matrix<T>(MatrixShape{131, 29}, Layout::RowMajor, 0);
        Matrix<T> transposed = make_matrix<T>(MatrixShape{131, 29}, Layout::ColumnMajor, 0);
        fill_matrix(left, stream, 12);
        fill_matrix(right, stream, 12);
        fill_matrix(transposed, stream, 12);
        Matrix<T> out = make_matrix<T>(MatrixShape{7, 29}, Layout::RowMajor, 0);
        assert(multiply_into(out, left.view(), right.view(), LaneCount::Native).is_ok());
        Matrix<T> other = make_matrix<T>(MatrixShape{7, 29}, Layout::ColumnMajor, 0);
        assert(multiply_into(other, left.view(), transposed.view(), LaneCount::Native).is_ok());
        Matrix<T> sums = make_matrix<T>(MatrixShape{1, 29}, Layout::RowMajor, 0);
        assert(sum_runs_into(sums, right.view(), LaneCount::Native).is_ok());
        Matrix<T> column_sums = make_matrix<T>(MatrixShape{1, 29}, Layout::RowMajor, 0);
        assert(sum_runs_into(column_sums, transposed.view(), LaneCount::Native).is_ok());
        for (std::size_t row = 0; row < 7; ++row) {
            for (std::size_t column = 0; column < 29; ++column) {
                digest = fold_value(digest, out.at(row, column));
                digest = fold_value(digest, other.at(row, column));
            }
        }
        for (std::size_t column = 0; column < 29; ++column) {
            digest = fold_value(digest, sums.at(0, column));
            digest = fold_value(digest, column_sums.at(0, column));
        }
    }
    ::fixy::HeapArray<T> firsts = ::fixy::mint_heap_array(rig_alloc(), 1000, T{});
    ::fixy::HeapArray<T> seconds = ::fixy::mint_heap_array(rig_alloc(), 1000, T{});
    for (T& value : firsts)
        value = draw_spread_value<T>(stream, 12);
    for (T& value : seconds)
        value = draw_spread_value<T>(stream, 12);
    for (std::size_t count = 990; count <= 1000; ++count) {
        ::fixy::View<T const> const left = ::fixy::View<T const>{firsts.view()}.window(0, count).expect("fits");
        ::fixy::View<T const> const right = ::fixy::View<T const>{seconds.view()}.window(0, count).expect("fits");
        digest = fold_value(digest, ::fixy::sum_products_by_tree(left, right).value_or(T{}));
    }
    return digest;
}

[[nodiscard]] ProductDigests digests_of_this_unit() noexcept {
    return ProductDigests{digest_of_fixed_draws<float>(), digest_of_fixed_draws<double>()};
}

}  // namespace

}  // namespace crucible::test
