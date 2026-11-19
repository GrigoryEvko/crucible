// Tests of foundation/core/Product.h: the tree products of the strided views.
//
// The model of each output is the scalar tree of THE ORDER in
// test/product_rig.h, which reads each element through its position in the
// array.  The oracle is the exact sum of test/exact_sum.h.  The test checks:
//   - each output has the bits of the model, for each path (dot, row and
//     scalar), each layout of the three views, each count of lanes, and each
//     size around the powers of two
//   - each output is inside the a priori bound of THE ERROR BOUND against the
//     exact sum, on drawn values of a wide exponent range
//   - the hard cases: the two zeros, the subnormal products, the
//     infinities, the NaN payloads that give the canonical NaN, and the
//     cancellations
//   - a split of the output into parts, as threads take them, gives the bits
//     of one call
//   - the shape and the overlap errors write nothing
//   - a digest of the outputs of fixed draws holds for each build: the
//     preset default (x86-64-v3), the presets release-v1 to release-v4 and
//     the release preset (the native target) give one digest

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/Reductions.h>
#include <fixy/check/Check.h>

#include <foundation/Simd.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/X86Level.h>

#include "../exact_sum.h"
#include "../product_rig.h"
#include "philox_stream.h"
#include "product_forms.h"
#include "product_units.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace {

using ::crucible::test::LaneCount;
using ::crucible::test::multiply_into;
using ::crucible::test::ProductDigests;
using ::crucible::test::sum_runs_into;

inline constexpr ::fixy::FixedArray<LaneCount, 6> kLaneCounts{
    LaneCount::Native, LaneCount::Two, LaneCount::Four, LaneCount::Eight, LaneCount::Sixteen, LaneCount::ThirtyTwo};

using ::crucible::test::BoundRecord;
using ::crucible::test::ExactSum;
using ::crucible::test::Layout;
using ::crucible::test::make_matrix;
using ::crucible::test::Matrix;
using ::crucible::test::MatrixShape;
using ::crucible::test::PhiloxStream;
using ::fixy::FixedArray;
using ::fixy::with_mut;

// A NaN with the sign bit and a payload, which no arithmetic gives.
template <class T>
[[nodiscard]] T payload_nan() noexcept {
    if constexpr (sizeof(T) == sizeof(float)) {
        return ::fixy::bit_cast<float>(std::uint32_t{0xFFC02345u});
    } else {
        return ::fixy::bit_cast<double>(std::uint64_t{0xFFF8000000002345u});
    }
}

// The scale that moves a drawn value near one to a value whose products with
// a value of the same scale underflow: 2^-75 for a float, 2^-537 for a double.
template <class T>
[[nodiscard]] T underflow_scale() noexcept {
    if constexpr (sizeof(T) == sizeof(float)) {
        return 0x1p-75f;
    } else {
        return 0x1p-537;
    }
}

// True when each output of out has the bits of the model, and each slot of
// the array outside the matrix keeps its NaN.
template <class T>
[[nodiscard]] bool has_model_bits(Matrix<T> const& out, Matrix<T> const& left, Matrix<T> const& right) noexcept {
    std::size_t matrix_slots = 0;
    for (std::size_t row = 0; row < out.shape.rows; ++row) {
        for (std::size_t column = 0; column < out.shape.columns; ++column) {
            ++matrix_slots;
            T const expected = ::crucible::test::model_product(left, right, row, column);
            if (::crucible::test::bits_of_float(out.at(row, column)) != ::crucible::test::bits_of_float(expected)) {
                return false;
            }
        }
    }
    std::size_t outside = 0;
    for (T const value : out.values) {
        if (::crucible::test::bits_of_float(value) == ::crucible::test::bits_of_float(-::fixy::quiet_nan_of<T>)) {
            ++outside;
        }
    }
    return outside + matrix_slots == out.values.size();
}

// The layout of a draw.
[[nodiscard]] Layout draw_layout(PhiloxStream& stream) noexcept {
    return ::fixy::enum_from<Layout>(static_cast<std::uint8_t>(stream.below(::crucible::test::kLayouts)))
        .expect("a draw below the count of the layouts");
}

// The count of the outputs whose left fold of the products has bits other
// than the tree: the positive control of the model tests, which shows that
// the drawn values tell a change of the order.
template <class T>
[[nodiscard]] std::size_t count_left_folds_that_differ(Matrix<T> const& out, Matrix<T> const& left,
                                                       Matrix<T> const& right) noexcept {
    std::size_t differ = 0;
    for (std::size_t row = 0; row < out.shape.rows; ++row) {
        for (std::size_t column = 0; column < out.shape.columns; ++column) {
            T fold = T{};
            for (std::size_t inner = 0; inner < left.shape.columns; ++inner) {
                T const product = left.at(row, inner) * right.at(inner, column);
                fold = fold + product;
            }
            if (::crucible::test::bits_of_float(fold) != ::crucible::test::bits_of_float(out.at(row, column))) ++differ;
        }
    }
    return differ;
}

// One drawn product: the sizes, the three layouts, the values.  Each output
// has the bits of the model for each count of lanes.  Gives the count of the
// outputs whose left fold differs from the tree.
template <class T>
[[nodiscard]] std::size_t check_drawn_product(PhiloxStream& stream, std::size_t rows, std::size_t columns,
                                              std::size_t depth, std::uint64_t spread) noexcept {
    Matrix<T> left = make_matrix<T>(MatrixShape{rows, depth}, draw_layout(stream), stream.below(3));
    Matrix<T> right = make_matrix<T>(MatrixShape{depth, columns}, draw_layout(stream), stream.below(3));
    ::crucible::test::fill_matrix(left, stream, spread);
    ::crucible::test::fill_matrix(right, stream, spread);
    Layout const out_layout = draw_layout(stream);
    std::size_t differ = 0;
    for (LaneCount const lanes : kLaneCounts) {
        Matrix<T> out = make_matrix<T>(MatrixShape{rows, columns}, out_layout, 1);
        CRUCIBLE_TEST_ASSERT(kTest, multiply_into(out, left.view(), right.view(), lanes).is_ok());
        CRUCIBLE_TEST_ASSERT(kTest, has_model_bits(out, left, right));
        if (lanes == LaneCount::Native) differ = count_left_folds_that_differ(out, left, right);
    }
    return differ;
}

// Each size from 0 to 20 on each axis, in each layout, and the sizes around
// each power of two of the depth to 1027, against the model.
template <class T>
void test_products_have_the_bits_of_the_model() {
    PhiloxStream stream{0x9D0C000000000001u};
    std::size_t cases = 0;
    std::size_t differ = 0;
    for (std::size_t rows = 0; rows <= 6; ++rows) {
        for (std::size_t columns = 0; columns <= 20; ++columns) {
            for (std::size_t depth = 0; depth <= 20; ++depth) {
                differ += check_drawn_product<T>(stream, rows, columns, depth, 12);
                ++cases;
            }
        }
    }
    for (std::size_t power = 32; power <= 1024; power *= 2) {
        for (std::size_t depth = power - 3; depth <= power + 3; ++depth) {
            differ += check_drawn_product<T>(stream, 1 + stream.below(4), 1 + stream.below(40), depth, 12);
            ++cases;
        }
    }
    CRUCIBLE_TEST_ASSERT(kTest, differ > 0);
    ::fixy::check::pass(kTest,
                        "test_core_product: {} drawn products of {} bytes in each layout and each count of lanes "
                        "have the bits of the model, and a left fold differs at {} outputs\n",
                        cases, sizeof(T), differ);
}

// The outputs are inside the a priori bound against the exact sum, for
// depths to 3000 and values with exponents in [-50, 50] for a float and in
// [-400, 400] for a double, so no partial sum overflows.
template <class T>
void test_products_are_inside_the_bound() {
    PhiloxStream stream{0x9D0C000000000002u};
    BoundRecord record{};
    std::uint64_t const spread = sizeof(T) == sizeof(float) ? 50 : 400;
    for (std::size_t round = 0; round < 60; ++round) {
        std::size_t const depth = round < 40 ? 1 + stream.below(64) : 1 + stream.below(3000);
        std::size_t const rows = 1 + stream.below(5);
        std::size_t const columns = 1 + stream.below(19);
        Matrix<T> left = make_matrix<T>(MatrixShape{rows, depth}, draw_layout(stream), 0);
        Matrix<T> right = make_matrix<T>(MatrixShape{depth, columns}, draw_layout(stream), 0);
        ::crucible::test::fill_matrix(left, stream, spread);
        ::crucible::test::fill_matrix(right, stream, spread);
        Matrix<T> out = make_matrix<T>(MatrixShape{rows, columns}, draw_layout(stream), 0);
        CRUCIBLE_TEST_ASSERT(kTest, multiply_into(out, left.view(), right.view(), LaneCount::Native).is_ok());
        for (std::size_t row = 0; row < rows; ++row) {
            for (std::size_t column = 0; column < columns; ++column) {
                auto const [exact, magnitude] = ::crucible::test::exact_product(left, right, row, column);
                ::crucible::test::check_bound(kTest, exact, magnitude, depth, 1, out.at(row, column), record);
            }
        }
    }
    ::fixy::check::pass(kTest,
                        "test_core_product: {} outputs of {} bytes are inside the bound, the largest error is {} "
                        "of the bound\n",
                        record.outputs, sizeof(T), record.largest_ratio);
}

// A product of one row and one column with the drawn terms.
template <class T>
[[nodiscard]] T product_of_terms(::fixy::View<T const> firsts, ::fixy::View<T const> seconds,
                                 LaneCount lanes) noexcept {
    std::size_t const depth = firsts.size();
    Matrix<T> left = make_matrix<T>(MatrixShape{1, depth}, Layout::RowMajor, 0);
    Matrix<T> right = make_matrix<T>(MatrixShape{depth, 1}, Layout::ColumnMajor, 0);
    for (std::size_t inner = 0; inner < depth; ++inner) {
        *left.values.view()
             .get(left.position(0, inner))
             .expect("a test reads an element at or past the count of its View") =
            *firsts.get(inner).expect("a test reads an element at or past the count of its View");
        *right.values.view()
             .get(right.position(inner, 0))
             .expect("a test reads an element at or past the count of its View") =
            *seconds.get(inner).expect("a test reads an element at or past the count of its View");
    }
    Matrix<T> out = make_matrix<T>(MatrixShape{1, 1}, Layout::RowMajor, 0);
    CRUCIBLE_TEST_ASSERT(kTest, multiply_into(out, left.view(), right.view(), lanes).is_ok());
    T const dot = out.at(0, 0);
    // The same terms on the row path and on the scalar path.
    Matrix<T> spread_left = make_matrix<T>(MatrixShape{1, depth}, Layout::Spread, 0);
    Matrix<T> row_right = make_matrix<T>(MatrixShape{depth, 1}, Layout::RowMajor, 0);
    for (std::size_t inner = 0; inner < depth; ++inner) {
        *spread_left.values.view()
             .get(spread_left.position(0, inner))
             .expect("a test reads an element at or past the count of its View") =
            *firsts.get(inner).expect("a test reads an element at or past the count of its View");
        *row_right.values.view()
             .get(row_right.position(inner, 0))
             .expect("a test reads an element at or past the count of its View") =
            *seconds.get(inner).expect("a test reads an element at or past the count of its View");
    }
    Matrix<T> row_out = make_matrix<T>(MatrixShape{1, 1}, Layout::RowMajor, 0);
    CRUCIBLE_TEST_ASSERT(kTest, multiply_into(row_out, spread_left.view(), row_right.view(), lanes).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest,
                         ::crucible::test::bits_of_float(row_out.at(0, 0)) == ::crucible::test::bits_of_float(dot));
    return dot;
}

// The hard cases with a known output.
template <class T>
void test_the_hard_cases() {
    using ::crucible::test::bits_of_float;
    T const zero = T{0};
    T const one = T{1};
    T const infinity = ::fixy::infinity_of<T>;
    T const tiny = ::fixy::min_subnormal_of<T>;
    T const canonical = ::fixy::quiet_nan_of<T>;
    for (LaneCount const lanes : kLaneCounts) {
        // A product of -0 in each term gives -0, and one +0 term gives +0.
        FixedArray<T, 37> negative_zeros{};
        FixedArray<T, 37> ones{};
        for (T& value : negative_zeros)
            value = -zero;
        for (T& value : ones)
            value = one;
        ::fixy::View<T const> const zeros_view{negative_zeros.view()};
        ::fixy::View<T const> const ones_view{ones.view()};
        CRUCIBLE_TEST_ASSERT(kTest,
                             bits_of_float(product_of_terms(zeros_view, ones_view, lanes)) == bits_of_float(-zero));
        *negative_zeros.get(17).expect("a test reads an element at or past the count of its array") = zero;
        CRUCIBLE_TEST_ASSERT(kTest,
                             bits_of_float(product_of_terms(zeros_view, ones_view, lanes)) == bits_of_float(zero));
        // An infinity gives itself, and two infinities of the two signs give
        // the canonical NaN.
        FixedArray<T, 37> mixed{};
        for (T& value : mixed)
            value = one;
        ::fixy::View<T const> const mixed_view{mixed.view()};
        *mixed.get(3).expect("a test reads an element at or past the count of its array") = infinity;
        CRUCIBLE_TEST_ASSERT(kTest,
                             bits_of_float(product_of_terms(mixed_view, ones_view, lanes)) == bits_of_float(infinity));
        *mixed.get(30).expect("a test reads an element at or past the count of its array") = -infinity;
        CRUCIBLE_TEST_ASSERT(kTest,
                             bits_of_float(product_of_terms(mixed_view, ones_view, lanes)) == bits_of_float(canonical));
        // A product of an infinity and a zero is a NaN, and each NaN of a
        // payload, a sign or a signaling bit gives the canonical NaN.
        *mixed.get(30).expect("a test reads an element at or past the count of its array") = one;
        *mixed.get(3).expect("a test reads an element at or past the count of its array") = infinity;
        *negative_zeros.get(3).expect("a test reads an element at or past the count of its array") = zero;
        CRUCIBLE_TEST_ASSERT(kTest, bits_of_float(product_of_terms(mixed_view, zeros_view, lanes))
                                        == bits_of_float(canonical));
        *mixed.get(3).expect("a test reads an element at or past the count of its array") = payload_nan<T>();
        CRUCIBLE_TEST_ASSERT(kTest,
                             bits_of_float(product_of_terms(mixed_view, ones_view, lanes)) == bits_of_float(canonical));
        *mixed.get(3).expect("a test reads an element at or past the count of its array") = ::fixy::signaling_nan_of<T>;
        CRUCIBLE_TEST_ASSERT(kTest,
                             bits_of_float(product_of_terms(mixed_view, ones_view, lanes)) == bits_of_float(canonical));
        // A cancellation: x0 * 1 + ... with the pairs x and -x, and one small
        // term, gives the small term exactly.
        FixedArray<T, 37> pairs{};
        for (std::size_t index = 0; index < 36; index += 2) {
            T const value = T{3} * static_cast<T>(index + 1);
            *pairs.get(index).expect("a test reads an element at or past the count of its array") = value;
            *pairs.get(index + 1).expect("a test reads an element at or past the count of its array") = -value;
        }
        *pairs.get(36).expect("a test reads an element at or past the count of its array") = tiny;
        CRUCIBLE_TEST_ASSERT(kTest,
                             bits_of_float(product_of_terms(::fixy::View<T const>{pairs.view()}, ones_view, lanes))
                                 == bits_of_float(tiny));
        // A product that underflows to a subnormal value keeps its bits, and
        // a sum of subnormal values is exact.
        FixedArray<T, 37> halves{};
        for (T& value : halves)
            value = T{0.5};
        FixedArray<T, 37> tinies{};
        for (T& value : tinies)
            value = tiny + tiny;
        CRUCIBLE_TEST_ASSERT(kTest, bits_of_float(product_of_terms(::fixy::View<T const>{halves.view()},
                                                                   ::fixy::View<T const>{tinies.view()}, lanes))
                                        == bits_of_float(tiny * T{37}));
    }
    ::fixy::check::pass(kTest,
                        "test_core_product: the zeros, the infinities, the NaNs, a cancellation and the "
                        "subnormal products of {} bytes give their stated bits\n",
                        sizeof(T));
}

// The hard cases inside the bound: drawn terms with subnormal products, a
// heavy cancellation and a spread of exponents of 200 binades.
template <class T>
void test_the_hard_cases_are_inside_the_bound() {
    PhiloxStream stream{0x9D0C000000000003u};
    BoundRecord record{};
    for (std::size_t round = 0; round < 200; ++round) {
        std::size_t const depth = 1 + stream.below(300);
        std::size_t const kind = round % 4;
        Matrix<T> left = make_matrix<T>(MatrixShape{2, depth}, Layout::RowMajor, 0);
        Matrix<T> right = make_matrix<T>(MatrixShape{depth, 3}, Layout::ColumnMajor, 0);
        for (std::size_t inner = 0; inner < depth; ++inner) {
            for (std::size_t row = 0; row < 2; ++row) {
                // Kind 1: products near and below the smallest subnormal
                // value.  Kind 2: each odd term cancels the term before it.
                T value = ::crucible::test::draw_spread_value<T>(stream, 30);
                if (kind == 1) value = ::crucible::test::draw_spread_value<T>(stream, 2) * underflow_scale<T>();
                if (kind == 2 && inner % 2 == 1) value = -left.at(row, inner - 1);
                *left.values.view()
                     .get(left.position(row, inner))
                     .expect("a test reads an element at or past the count of its View") = value;
            }
            for (std::size_t column = 0; column < 3; ++column) {
                // Kind 3: the products spread over more than 200 binades, and
                // their sum stays below the largest float.
                std::uint64_t const spread = kind != 3 ? 30 : sizeof(T) == sizeof(float) ? 85 : 400;
                T value = ::crucible::test::draw_spread_value<T>(stream, spread);
                if (kind == 1) value = ::crucible::test::draw_spread_value<T>(stream, 2) * underflow_scale<T>();
                if (kind == 2 && inner % 2 == 1) value = right.at(inner - 1, column);
                *right.values.view()
                     .get(right.position(inner, column))
                     .expect("a test reads an element at or past the count of its View") = value;
            }
        }
        Matrix<T> out = make_matrix<T>(MatrixShape{2, 3}, Layout::RowMajor, 0);
        CRUCIBLE_TEST_ASSERT(kTest, multiply_into(out, left.view(), right.view(), LaneCount::Native).is_ok());
        for (std::size_t row = 0; row < 2; ++row) {
            for (std::size_t column = 0; column < 3; ++column) {
                auto const [exact, magnitude] = ::crucible::test::exact_product(left, right, row, column);
                ::crucible::test::check_bound(kTest, exact, magnitude, depth, 1, out.at(row, column), record);
                CRUCIBLE_TEST_ASSERT(kTest, ::crucible::test::bits_of_float(out.at(row, column))
                                                == ::crucible::test::bits_of_float(
                                                    ::crucible::test::model_product(left, right, row, column)));
            }
        }
    }
    ::fixy::check::pass(kTest,
                        "test_core_product: {} hard outputs of {} bytes are inside the bound, the largest error "
                        "is {} of the bound\n",
                        record.outputs, sizeof(T), record.largest_ratio);
}

// A split of the output into blocks of rows, as threads take them, gives the
// bits of one call.  Each block is its own call on a slice of the left
// factor, with its own part of the output run.
template <class T>
void test_a_split_of_the_rows_changes_no_bit() {
    PhiloxStream stream{0x9D0C000000000004u};
    std::size_t const rows = 23;
    std::size_t const columns = 37;
    std::size_t const depth = 70;
    Matrix<T> left = make_matrix<T>(MatrixShape{rows, depth}, Layout::RowMajor, 0);
    Matrix<T> right = make_matrix<T>(MatrixShape{depth, columns}, Layout::RowMajor, 0);
    ::crucible::test::fill_matrix(left, stream, 12);
    ::crucible::test::fill_matrix(right, stream, 12);
    Matrix<T> whole = make_matrix<T>(MatrixShape{rows, columns}, Layout::RowMajor, 0);
    CRUCIBLE_TEST_ASSERT(kTest, multiply_into(whole, left.view(), right.view(), LaneCount::Native).is_ok());
    FixedArray<std::size_t, 5> const block_rows{1, 2, 5, 8, 23};
    for (std::size_t const block : block_rows) {
        ::fixy::HeapArray<T> parts = ::fixy::mint_heap_array(::crucible::test::rig_alloc(), rows * columns, T{});
        parts = with_mut(std::move(parts), [&](auto run) noexcept {
            auto chunks = ::fixy::split_chunks(std::move(run), block * columns);
            std::size_t first_row = 0;
            while (true) {
                auto next = chunks.next();
                if (next.is_none()) break;
                auto chunk = std::move(next).expect("the test above");
                std::size_t const count = chunk.size() / columns;
                FixedArray<std::size_t, 2> const sizes{count, columns};
                FixedArray<std::size_t, 2> const strides{columns, 1};
                auto target = ::fixy::mint_strided_view_mut(std::move(chunk), sizes.view(), strides.view(), 0)
                                  .expect("a block fits its chunk");
                auto const rows_of_block = left.view()
                                               .slice(::fixy::index<0>(), first_row, count)
                                               .expect("the block is inside the left factor");
                CRUCIBLE_TEST_ASSERT(
                    kTest, ::fixy::multiply_by_tree(target, ::fixy::side_as<::fixy::side::LeftFactor>(rows_of_block),
                                                    ::fixy::side_as<::fixy::side::RightFactor>(right.view()))
                               .is_ok());
                first_row += count;
            }
        });
        for (std::size_t position = 0; position < rows * columns; ++position) {
            CRUCIBLE_TEST_ASSERT(kTest,
                                 ::crucible::test::bits_of_float(*parts.view().get(position).expect(
                                     "a test reads an element at or past the count of its View"))
                                     == ::crucible::test::bits_of_float(*whole.values.view().get(position).expect(
                                         "a test reads an element at or past the count of its View")));
        }
    }
    ::fixy::check::pass(kTest,
                        "test_core_product: each split of the rows of a product of {} bytes into blocks gives "
                        "the bits of one call\n",
                        sizeof(T));
}

// The errors come before the first write.
void test_the_errors_write_nothing() {
    PhiloxStream stream{0x9D0C000000000005u};
    Matrix<float> left = make_matrix<float>(MatrixShape{3, 4}, Layout::RowMajor, 0);
    Matrix<float> right = make_matrix<float>(MatrixShape{5, 2}, Layout::RowMajor, 0);
    ::crucible::test::fill_matrix(left, stream, 4);
    ::crucible::test::fill_matrix(right, stream, 4);
    Matrix<float> out = make_matrix<float>(MatrixShape{3, 2}, Layout::RowMajor, 0);
    auto const shape_error = multiply_into(out, left.view(), right.view(), LaneCount::Native);
    CRUCIBLE_TEST_ASSERT(kTest, shape_error.is_err());
    CRUCIBLE_TEST_ASSERT(kTest, shape_error.err().value_or(::fixy::ProductError::OutputOverlapsInput)
                                    == ::fixy::ProductError::ShapeMismatch);
    for (float const value : out.values) {
        CRUCIBLE_TEST_ASSERT(kTest, ::crucible::test::bits_of_float(value) == 0u);
    }
    // The output is the left factor itself.
    Matrix<float> square = make_matrix<float>(MatrixShape{4, 4}, Layout::RowMajor, 0);
    ::crucible::test::fill_matrix(square, stream, 4);
    Matrix<float> other = make_matrix<float>(MatrixShape{4, 4}, Layout::ColumnMajor, 0);
    ::crucible::test::fill_matrix(other, stream, 4);
    ::fixy::HeapArray<float> const before = ::fixy::mint_heap_array_copy<float>(
        ::crucible::test::rig_alloc(), ::fixy::View<float const>{square.values.view()});
    FixedArray<std::size_t, 2> const sizes{4, 4};
    FixedArray<std::size_t, 2> const strides{4, 1};
    auto [overlap, back] = with_mut(std::move(square.values), [&](auto run) noexcept {
        ::fixy::StridedView<float const, 2> const self_view =
            ::fixy::mint_strided_view(::fixy::View<float const>{run.view()}, sizes.view(), strides.view(), 0)
                .expect("the square fits");
        auto target =
            ::fixy::mint_strided_view_mut(std::move(run), sizes.view(), strides.view(), 0).expect("the square fits");
        return ::fixy::multiply_by_tree(target, ::fixy::side_as<::fixy::side::LeftFactor>(self_view),
                                        ::fixy::side_as<::fixy::side::RightFactor>(other.view()));
    });
    square.values = std::move(back);
    CRUCIBLE_TEST_ASSERT(kTest, overlap.is_err());
    CRUCIBLE_TEST_ASSERT(kTest, overlap.err().value_or(::fixy::ProductError::ShapeMismatch)
                                    == ::fixy::ProductError::OutputOverlapsInput);
    for (std::size_t position = 0; position < 16; ++position) {
        CRUCIBLE_TEST_ASSERT(kTest, ::crucible::test::bits_of_float(*square.values.view().get(position).expect(
                                        "a test reads an element at or past the count of its View"))
                                        == ::crucible::test::bits_of_float(*before.view().get(position).expect(
                                            "a test reads an element at or past the count of its View")));
    }
    ::fixy::check::pass(kTest, "test_core_product: a shape that does not agree and an output over a factor give their "
                               "error and write nothing\n");
}

// Each sum of runs has the bits of the model in each layout and for each
// count of lanes, and is inside its bound.
template <class T>
void test_sums_of_runs() {
    PhiloxStream stream{0x9D0C000000000006u};
    BoundRecord record{};
    std::size_t cases = 0;
    for (std::size_t count = 0; count <= 40; ++count) {
        for (std::size_t outputs = 0; outputs <= 37; outputs += 1 + stream.below(5)) {
            Matrix<T> runs = make_matrix<T>(MatrixShape{count, outputs}, draw_layout(stream), stream.below(3));
            ::crucible::test::fill_matrix(runs, stream, 40);
            Layout const out_layout = stream.below(2) == 0 ? Layout::RowMajor : Layout::Spread;
            for (LaneCount const lanes : kLaneCounts) {
                Matrix<T> out = make_matrix<T>(MatrixShape{1, outputs}, out_layout, 1);
                CRUCIBLE_TEST_ASSERT(kTest, sum_runs_into(out, runs.view(), lanes).is_ok());
                for (std::size_t column = 0; column < outputs; ++column) {
                    CRUCIBLE_TEST_ASSERT(
                        kTest, ::crucible::test::bits_of_float(out.at(0, column))
                                   == ::crucible::test::bits_of_float(::crucible::test::model_runs(runs, column)));
                    if (lanes != LaneCount::Native) continue;
                    ExactSum<T> exact{};
                    ExactSum<T> magnitude{};
                    for (std::size_t run = 0; run < count; ++run) {
                        exact.add(runs.at(run, column));
                        magnitude.add(::fixy::fp::abs(runs.at(run, column)));
                    }
                    ::crucible::test::check_bound(kTest, exact, magnitude, count, 0, out.at(0, column), record);
                }
            }
            ++cases;
        }
    }
    ::fixy::check::pass(kTest,
                        "test_core_product: {} sums of runs of {} bytes have the bits of the model, and {} "
                        "outputs are inside the bound, the largest error is {} of the bound\n",
                        cases, sizeof(T), record.outputs, record.largest_ratio);
}

// sum_products_by_tree has the bits of the model, for each count to 300 and
// each count of lanes, and a length mismatch gives its error.
template <class T>
void test_dot_products() {
    PhiloxStream stream{0x9D0C000000000007u};
    ::fixy::HeapArray<T> firsts = ::fixy::mint_heap_array(::crucible::test::rig_alloc(), 300, T{});
    ::fixy::HeapArray<T> seconds = ::fixy::mint_heap_array(::crucible::test::rig_alloc(), 300, T{});
    for (T& value : firsts)
        value = ::crucible::test::draw_spread_value<T>(stream, 12);
    for (T& value : seconds)
        value = ::crucible::test::draw_spread_value<T>(stream, 12);
    for (std::size_t count = 0; count <= 300; ++count) {
        ::fixy::View<T const> const left = ::fixy::View<T const>{firsts.view()}.window(0, count).expect("fits");
        ::fixy::View<T const> const right = ::fixy::View<T const>{seconds.view()}.window(0, count).expect("fits");
        T expected = T{};
        if (count > 0) {
            auto const leaf = [&](std::size_t index) noexcept {
                T const product = *left.get(index).expect("a test reads an element at or past the count of its View")
                                * *right.get(index).expect("a test reads an element at or past the count of its View");
                return product;
            };
            expected = ::crucible::test::model_tree<T>(leaf, 0, count);
        }
        auto const bits = [](::fixy::Result<T, ::fixy::LengthMismatch> result) noexcept {
            return ::crucible::test::bits_of_float(std::move(result).value_or(::fixy::quiet_nan_of<T>));
        };
        auto const expected_bits = ::crucible::test::bits_of_float(expected);
        CRUCIBLE_TEST_ASSERT(kTest, bits(::fixy::sum_products_by_tree(left, right)) == expected_bits);
        CRUCIBLE_TEST_ASSERT(kTest, bits(::fixy::sum_products_by_tree(right, left)) == expected_bits);
        CRUCIBLE_TEST_ASSERT(kTest, bits(::fixy::sum_products_by_tree_in_lanes<2>(left, right)) == expected_bits);
        CRUCIBLE_TEST_ASSERT(kTest, bits(::fixy::sum_products_by_tree_in_lanes<4>(left, right)) == expected_bits);
        CRUCIBLE_TEST_ASSERT(kTest, bits(::fixy::sum_products_by_tree_in_lanes<8>(left, right)) == expected_bits);
        CRUCIBLE_TEST_ASSERT(kTest, bits(::fixy::sum_products_by_tree_in_lanes<16>(left, right)) == expected_bits);
        CRUCIBLE_TEST_ASSERT(kTest, bits(::fixy::sum_products_by_tree_in_lanes<32>(left, right)) == expected_bits);
    }
    ::fixy::View<T const> const longer = ::fixy::View<T const>{firsts.view()}.window(0, 5).expect("fits");
    ::fixy::View<T const> const shorter = ::fixy::View<T const>{seconds.view()}.window(0, 4).expect("fits");
    CRUCIBLE_TEST_ASSERT(kTest, ::fixy::sum_products_by_tree(longer, shorter).is_err());
    ::fixy::check::pass(kTest,
                        "test_core_product: the dot products of {} bytes to 300 terms have the bits of the model "
                        "for each count of lanes\n",
                        sizeof(T));
}

// The digests of the fixed draws of product_forms.h, which each build gives.
// The tree fixes each bit, so a change of a digest is a change of the stated
// order or of a draw.  Take a new digest only from two units of different
// vector widths that agree, and give the reason in the change.
inline constexpr std::uint64_t kFloatDigest = 2201208286362688169u;
inline constexpr std::uint64_t kDoubleDigest = 3087225960675716927u;

[[nodiscard]] bool agree_with_the_constants(ProductDigests digests) noexcept {
    if (digests.float_digest != kFloatDigest || digests.double_digest != kDoubleDigest) {
        ::fixy::report(::fixy::Sink::Err, "test_core_product: the digests are {} and {}\n", digests.float_digest,
                       digests.double_digest);
    }
    return digests.float_digest == kFloatDigest && digests.double_digest == kDoubleDigest;
}

// The digests of this unit, and of the units of x86-64-v3 and x86-64-v4 that
// the build has on a host of their level, hold.
void test_the_digests_hold() {
    CRUCIBLE_TEST_ASSERT(kTest, agree_with_the_constants(::crucible::test::digests_of_this_unit(kTest)));
    std::size_t units = 1;
#if defined(__x86_64__)
    namespace os = ::foundation::os;
    // The build compiles the unit of each level other than the level of the
    // build (test/foundation/CMakeLists.txt).
    constexpr bool has_v3_unit = CRUCIBLE_TEST_HAS_V3_UNIT != 0;
    constexpr bool has_v4_unit = CRUCIBLE_TEST_HAS_V4_UNIT != 0;
    os::X86Level const host = os::x86_level_of(os::read_x86_feature_words());
    if constexpr (has_v3_unit) {
        if (host >= os::X86Level::v3) {
            CRUCIBLE_TEST_ASSERT(kTest, agree_with_the_constants(::crucible::test::product_digests_of_v3_unit()));
            ++units;
        }
    }
    if constexpr (has_v4_unit) {
        if (host >= os::X86Level::v4) {
            CRUCIBLE_TEST_ASSERT(kTest, agree_with_the_constants(::crucible::test::product_digests_of_v4_unit()));
            ++units;
        }
    }
#endif
    ::fixy::check::pass(kTest,
                        "test_core_product: the digests of the fixed draws hold in {} units of {} vector bytes "
                        "and more\n",
                        units, ::foundation::simd::native_vector_bytes);
}

}  // namespace

int main() {
    test_products_have_the_bits_of_the_model<float>();
    test_products_have_the_bits_of_the_model<double>();
    test_products_are_inside_the_bound<float>();
    test_products_are_inside_the_bound<double>();
    test_the_hard_cases<float>();
    test_the_hard_cases<double>();
    test_the_hard_cases_are_inside_the_bound<float>();
    test_the_hard_cases_are_inside_the_bound<double>();
    test_a_split_of_the_rows_changes_no_bit<float>();
    test_a_split_of_the_rows_changes_no_bit<double>();
    test_the_errors_write_nothing();
    test_sums_of_runs<float>();
    test_sums_of_runs<double>();
    test_dot_products<float>();
    test_dot_products<double>();
    test_the_digests_hold();
    return 0;
}
