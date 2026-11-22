#pragma once

// The tree products of the Region family: the dot product, the matrix
// product of two strided views, and the sum of the runs of a strided view,
// each in one stated order.  The result has the same bits for each thread
// count, each split of the output into parts, each count of SIMD lanes and
// each host.
//
//   sum_products_by_tree(left, right)
//                       the tree sum of the products left[i] * right[i] of
//                       two Views, or LengthMismatch when their counts
//                       differ.  The two arguments can swap with no change of
//                       a bit, because each product commutes
//   multiply_by_tree(out, side_as<side::LeftFactor>(a),
//                    side_as<side::RightFactor>(b))
//                       the matrix product of the views a of the sizes [M, K]
//                       and b of the sizes [K, N] into the unique view out of
//                       the sizes [M, N]: out[m, n] is the tree over k of
//                       a[m, k] * b[k, n]
//   sum_strided_runs_by_tree(out, runs)
//                       the tree sum of the R runs of a view of the sizes
//                       [R, N], element by element, into the unique view out
//                       of the size N: out[n] is the tree over r of
//                       runs[r, n]
//   sum_products_by_tree_in_lanes<L>, multiply_by_tree_in_lanes<L> and
//   sum_strided_runs_by_tree_in_lanes<L>
//                       the same forms with L lanes in each vector, for a test
//                       of the rule that the lanes change no bit
// The factors take the roles side::LeftFactor and side::RightFactor of
// foundation/core/Side.h, so the two factors of a call cannot swap.  Each
// factor is a view of const elements.
//
// Order
//   Each product a[m, k] * b[k, n] rounds alone, and the build contracts no
//   multiply and add (-ffp-contract=off).  The products of one output combine
//   by the tree of the order of foundation/core/Reduce.h over k = 0 to K - 1:
//   a run splits at the largest power of two below its count, and the part of
//   the lower k is the left operand of the add.  The tree of 3 products is
//   (p0 + p1) + p2.  No initial value enters the tree, so a product of -0 is
//   an output of -0.  A depth K of zero gives +0 at each output.  A sum of
//   runs combines runs[r, n] by the same tree over r, and no run gives +0.
//
// Paths
//   Each output takes one of three paths.  Each path computes the nodes of
//   the tree and no other sum, so each path gives the bits of the tree:
//     Dot     a has a stride of one on k, and b has a stride of one on k.  The
//             two runs of each output are a dot product, as in
//             sum_products_by_tree: L consecutive products fill one vector,
//             and the merges of the even and the odd lanes compute the nodes
//             of the tree (the lanes of Reduce.h)
//     Row     b and out have a stride of one on n.  The L outputs of one
//             vector are the L lanes, and each lane holds the tree of its own
//             output: each leaf is a[m, k] in each lane times the L elements
//             of b at k.  The last vector of a row takes a prefix load and a
//             prefix store of the outputs that remain
//     Scalar  each other layout: the tree of each output with the strides
//   A sum of runs has the same three paths: each run of one output is
//   consecutive (the tree sum of Reduce.h), the outputs are consecutive (one
//   lane for each output), or the scalar tree.  So the path, the lanes, a
//   split of the output and the target change no bit.  A test compares each
//   path and each count of lanes with a scalar model of the tree.
//
// Error bound
//   With the round to nearest of IEEE 754, the unit roundoff u (2^-24 for
//   float and 2^-53 for double), gamma(j) = j * u / (1 - j * u), and the depth
//   d = count_tree_depth(K), the computed output c' of the exact sum c of the
//   products holds
//     |c' - c| <= gamma(d + 1) * sum_k |a[m, k] * b[k, n]|
//                 + (1 + gamma(d)) * K * eta
//   with eta the largest error of a product that underflows: 2^-150 for float
//   and 2^-1075 for double.  A product rounds one time, and each add on the
//   path from a product to the root rounds one time.  An add with a
//   subnormal result is exact, so an add adds no term for an underflow.  The
//   bound needs d * u < 1 and no partial sum that overflows (Higham, Accuracy
//   and Stability of Numerical Algorithms, second edition, sections 2.2, 3.1
//   and 4.2).  A sum of runs has no product, so |c' - c| <= gamma(d) * sum_r
//   |runs[r, n]| with d = count_tree_depth(R).
//
// Values outside the bound
//   A NaN, an infinity or an overflow of a partial sum gives a NaN or an
//   infinity.  Each output that is a NaN is the canonical NaN quiet_nan_of<T>
//   of foundation/core/Number.h, as for the sums of Reduce.h, so no payload
//   and no order of the operands of a target reaches an output.  Each other
//   output keeps the bits of its tree.
//
// Aliases
//   The output must share no element with an input, because a write of one
//   output would change a later read.  Else a call gives OutputOverlapsInput
//   and writes nothing.  The check compares the address ranges of the views
//   as integers, so two views of different objects make no undefined compare
//   of pointers.  Two ranges that touch count as shared, also when the two
//   views interleave and share no element.
//
// Costs
//   The row path makes one broadcast, one load, one multiply and one add for
//   each leaf of a vector of L outputs, with about K / 2 calls of the tree
//   for each vector.  The dot path makes two loads, one multiply and about
//   two shuffles and one add for each vector of L products.  No operation
//   allocates.
//
// Target
//   Each form reads the vector width of the target, so the forms and their
//   SIMD helpers are in the inline namespace CRUCIBLE_SIMD_TARGET (the target
//   namespace of foundation/Platform.h).  The roles, the error type and the
//   scalar tree read no target, and they stay outside it.

#include <foundation/Simd.h>
#include <foundation/core/Bits.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Float.h>
#include <foundation/core/Number.h>
#include <foundation/core/Reduce.h>
#include <foundation/core/Region.h>
#include <foundation/core/Side.h>
#include <foundation/core/Strided.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace foundation::core {

// The roles of the two factors of a matrix product (foundation/core/Side.h).
namespace side {

// The left factor: a view of the sizes [M, K].
struct LeftFactor final {};

// The right factor: a view of the sizes [K, N].
struct RightFactor final {};

}  // namespace side

// The factors of a matrix product of elements T: views of const elements of
// rank 2 in their roles.
template <class T>
using LeftFactorOf = Side<side::LeftFactor, StridedView<T const, 2>>;
template <class T>
using RightFactorOf = Side<side::RightFactor, StridedView<T const, 2>>;

// The error of a matrix product and of a sum of runs.
enum class ProductError : std::uint8_t {
    // The sizes of the views do not agree.
    ShapeMismatch = 0,
    // The output shares an element with an input (the aliases).
    OutputOverlapsInput = 1,
};

namespace detail {

// True when the address ranges of the elements of two views that hold an
// element touch.  O(rank).
template <class T, std::size_t Rank, class U, std::size_t OtherRank>
[[nodiscard]] bool shares_elements_(StridedView<T, Rank> const& first,
                                    StridedView<U, OtherRank> const& second) noexcept {
    if (first.count() == 0 || second.count() == 0) return false;
    auto const range_end = [](auto const& view, std::uintptr_t low) noexcept {
        auto const& lanes = strided_lanes_of_(view);
        std::size_t span = 0;
        for (std::size_t axis = 0; axis < lanes.rank(); ++axis) {
            span += (lanes.sizes[axis] - 1) * lanes.strides[axis];
        }
        return low + (span + 1) * sizeof(typename std::remove_cvref_t<decltype(view)>::element_type);
    };
    std::uintptr_t const first_low = __builtin_bit_cast(std::uintptr_t, strided_first_of_(first));
    std::uintptr_t const second_low = __builtin_bit_cast(std::uintptr_t, strided_first_of_(second));
    return first_low < range_end(second, second_low) && second_low < range_end(first, first_low);
}

// The leaves of a scalar tree of products: leaf i is
// left[i * left_step] * right[i * right_step].
template <class T>
struct ProductLeaves {
    T const* left = nullptr;
    T const* right = nullptr;
    std::size_t left_step = 0;
    std::size_t right_step = 0;

    [[nodiscard]] CRUCIBLE_INLINE T operator()(std::size_t leaf) const noexcept {
        return left[leaf * left_step] * right[leaf * right_step];
    }
};

// The leaves of a scalar tree of a run: leaf i is first[i * step].
template <class T>
struct RunLeaves {
    T const* first = nullptr;
    std::size_t step = 0;

    [[nodiscard]] CRUCIBLE_INLINE T operator()(std::size_t leaf) const noexcept { return first[leaf * step]; }
};

// The tree of the count leaves from the leaf first, by the order, for a
// count of at least one.  Value is a scalar, or a vector whose lanes are
// independent trees.  A run of up to four leaves has its tree inline, so the
// recursion makes about one call for each two leaves.  The check file of this
// header compares the tree with the tree of reduce_by_tree for each count to
// 64, with an add that is neither associative nor commutative.
template <class Value, class Leaves>
[[nodiscard]] constexpr Value tree_of_leaves_(Leaves const& leaves, std::size_t first, std::size_t count) noexcept {
    switch (count) {
        case 1:
            return leaves(first);
        case 2:
            return leaves(first) + leaves(first + 1);
        case 3: {
            Value const pair = leaves(first) + leaves(first + 1);
            return pair + leaves(first + 2);
        }
        case 4: {
            Value const low = leaves(first) + leaves(first + 1);
            Value const high = leaves(first + 2) + leaves(first + 3);
            return low + high;
        }
        default:
            break;
    }
    std::size_t const split = bit_floor(count - 1);
    Value const left = tree_of_leaves_<Value>(leaves, first, split);
    Value const right = tree_of_leaves_<Value>(leaves, first + split, count - split);
    return left + right;
}

// The loads of a vector of the row path: each lane, or the first lanes.
enum class LaneFill : std::uint8_t {
    Full = 0,
    Prefix = 1,
};

inline namespace CRUCIBLE_SIMD_TARGET {

// The load of the lanes of one vector of the row path from first.
template <class T, int Lanes, LaneFill Fill>
[[nodiscard]] CRUCIBLE_INLINE simd::vec<T, Lanes> load_lanes_(T const* first, std::size_t active) noexcept {
    if constexpr (Fill == LaneFill::Full) {
        static_cast<void>(active);
        return simd::load<simd::vec<T, Lanes>>(first);
    } else {
        return simd::load_prefix<simd::vec<T, Lanes>>(first, active);
    }
}

// The leaves of the row path of a matrix product: lane j of leaf k is
// left[k * left_step] * right[k * right_step + j], for the active lanes.
template <class T, int Lanes, LaneFill Fill>
struct ProductLaneLeaves {
    T const* left = nullptr;
    T const* right = nullptr;
    std::size_t left_step = 0;
    std::size_t right_step = 0;
    std::size_t active = 0;

    [[nodiscard]] CRUCIBLE_INLINE simd::vec<T, Lanes> operator()(std::size_t leaf) const noexcept {
        return simd::vec<T, Lanes>{left[leaf * left_step]}
             * load_lanes_<T, Lanes, Fill>(right + leaf * right_step, active);
    }
};

// The leaves of the row path of a sum of runs: lane j of leaf r is
// first[r * step + j], for the active lanes.
template <class T, int Lanes, LaneFill Fill>
struct RunLaneLeaves {
    T const* first = nullptr;
    std::size_t step = 0;
    std::size_t active = 0;

    [[nodiscard]] CRUCIBLE_INLINE simd::vec<T, Lanes> operator()(std::size_t leaf) const noexcept {
        return load_lanes_<T, Lanes, Fill>(first + leaf * step, active);
    }
};

// The vector of a block of Vectors vectors of products from left and right,
// a power of two of at most 16.  Lane i holds the tree of the Vectors
// products that start at the product i * Vectors.
template <class T, int Lanes, std::size_t Vectors>
[[nodiscard]] CRUCIBLE_INLINE simd::vec<T, Lanes> product_block_fixed_(T const* left, T const* right) noexcept {
    using Vector = simd::vec<T, Lanes>;
    if constexpr (Vectors == 1) {
        return simd::load<Vector>(left) * simd::load<Vector>(right);
    } else {
        constexpr std::size_t half = Vectors / 2;
        constexpr std::size_t offset = half * static_cast<std::size_t>(Lanes);
        Vector const low = product_block_fixed_<T, Lanes, half>(left, right);
        Vector const high = product_block_fixed_<T, Lanes, half>(left + offset, right + offset);
        return merge_tree_vectors_<T, Lanes>(low, high);
    }
}

// The vector of a block of vectors vectors of products, a power of two.  A
// block of more than 16 vectors is the merge of its two halves.
template <class T, int Lanes>
[[nodiscard]] simd::vec<T, Lanes> product_block_vector_(T const* left, T const* right, std::size_t vectors) noexcept {
    switch (vectors) {
        case 1:
            return product_block_fixed_<T, Lanes, 1>(left, right);
        case 2:
            return product_block_fixed_<T, Lanes, 2>(left, right);
        case 4:
            return product_block_fixed_<T, Lanes, 4>(left, right);
        case 8:
            return product_block_fixed_<T, Lanes, 8>(left, right);
        case 16:
            return product_block_fixed_<T, Lanes, 16>(left, right);
        default:
            break;
    }
    std::size_t const half = vectors / 2;
    std::size_t const offset = half * static_cast<std::size_t>(Lanes);
    simd::vec<T, Lanes> const low = product_block_vector_<T, Lanes>(left, right, half);
    simd::vec<T, Lanes> const high = product_block_vector_<T, Lanes>(left + offset, right + offset, half);
    return merge_tree_vectors_<T, Lanes>(low, high);
}

// The tree of the count products of two consecutive runs, with vectors of
// Lanes lanes, for a count of at least one.  A part of fewer than Lanes
// products takes the scalar tree, a part of a power of two takes a block of
// vectors, and each other part splits by the order.
template <class T, int Lanes>
[[nodiscard]] T product_run_by_tree_(T const* left, T const* right, std::size_t count) noexcept {
    constexpr auto lanes = static_cast<std::size_t>(Lanes);
    if (count < lanes) return tree_of_leaves_<T>(ProductLeaves<T>{left, right, 1, 1}, 0, count);
    if (has_single_bit(count)) {
        return reduce_tree_lanes_<T, Lanes>(product_block_vector_<T, Lanes>(left, right, count / lanes));
    }
    std::size_t const split = bit_floor(count - 1);
    T const low = product_run_by_tree_<T, Lanes>(left, right, split);
    T const high = product_run_by_tree_<T, Lanes>(left + split, right + split, count - split);
    return low + high;
}

// The row path of one row of outputs: each vector of Lanes outputs from
// out, and the prefix of the outputs that remain.  make_leaves gives the
// leaves of the vector at an output position for a fill and a count of
// active lanes.
template <class T, int Lanes, class MakeLeaves>
void write_rows_of_lanes_(T* out, std::size_t outputs, std::size_t leaves, MakeLeaves const& make_leaves) noexcept {
    using Vector = simd::vec<T, Lanes>;
    constexpr auto lanes = static_cast<std::size_t>(Lanes);
    std::size_t position = 0;
    for (; position + lanes <= outputs; position += lanes) {
        Vector const sums =
            tree_of_leaves_<Vector>(make_leaves.template operator()<LaneFill::Full>(position, lanes), 0, leaves);
        simd::store(simd::canonicalize_nans(sums), out + position);
    }
    if (position < outputs) {
        std::size_t const active = outputs - position;
        Vector const sums =
            tree_of_leaves_<Vector>(make_leaves.template operator()<LaneFill::Prefix>(position, active), 0, leaves);
        simd::store_prefix(simd::canonicalize_nans(sums), out + position, active);
    }
}

// The matrix product on the plain views, with Lanes lanes (the paths).
template <class T, int Lanes>
[[nodiscard]] Result<Unit, ProductError> multiply_by_tree_(StridedView<T, 2> const& out,
                                                           StridedView<T const, 2> const& left,
                                                           StridedView<T const, 2> const& right) noexcept {
    auto const& out_lanes = strided_lanes_of_(out);
    auto const& left_lanes = strided_lanes_of_(left);
    auto const& right_lanes = strided_lanes_of_(right);
    std::size_t const rows = out_lanes.sizes[0];
    std::size_t const columns = out_lanes.sizes[1];
    std::size_t const depth = left_lanes.sizes[1];
    if (left_lanes.sizes[0] != rows || right_lanes.sizes[0] != depth || right_lanes.sizes[1] != columns) {
        return err(ProductError::ShapeMismatch);
    }
    if (rows == 0 || columns == 0) return Unit{};
    if (depth != 0 && (shares_elements_(out, left) || shares_elements_(out, right))) {
        return err(ProductError::OutputOverlapsInput);
    }
    T* const target = strided_first_of_(out);
    std::size_t const out_row = out_lanes.strides[0];
    std::size_t const out_column = out_lanes.strides[1];
    if (depth == 0) {
        for (std::size_t row = 0; row < rows; ++row) {
            for (std::size_t column = 0; column < columns; ++column) {
                target[row * out_row + column * out_column] = T{};
            }
        }
        return Unit{};
    }
    T const* const left_first = strided_first_of_(left);
    T const* const right_first = strided_first_of_(right);
    std::size_t const left_row = left_lanes.strides[0];
    std::size_t const left_step = left_lanes.strides[1];
    std::size_t const right_step = right_lanes.strides[0];
    std::size_t const right_column = right_lanes.strides[1];
    bool const is_dot = depth == 1 || (left_step == 1 && right_step == 1);
    bool const is_row = columns == 1 || (right_column == 1 && out_column == 1);
    if (is_dot) {
        for (std::size_t row = 0; row < rows; ++row) {
            for (std::size_t column = 0; column < columns; ++column) {
                target[row * out_row + column * out_column] = fp::canonicalize_nan(product_run_by_tree_<T, Lanes>(
                    left_first + row * left_row, right_first + column * right_column, depth));
            }
        }
        return Unit{};
    }
    if (is_row) {
        for (std::size_t row = 0; row < rows; ++row) {
            T const* const left_of_row = left_first + row * left_row;
            auto const make_leaves = [&]<LaneFill Fill>(std::size_t position, std::size_t active) noexcept {
                return ProductLaneLeaves<T, Lanes, Fill>{left_of_row, right_first + position, left_step, right_step,
                                                         active};
            };
            write_rows_of_lanes_<T, Lanes>(target + row * out_row, columns, depth, make_leaves);
        }
        return Unit{};
    }
    for (std::size_t row = 0; row < rows; ++row) {
        for (std::size_t column = 0; column < columns; ++column) {
            ProductLeaves<T> const leaves{left_first + row * left_row, right_first + column * right_column, left_step,
                                          right_step};
            target[row * out_row + column * out_column] = fp::canonicalize_nan(tree_of_leaves_<T>(leaves, 0, depth));
        }
    }
    return Unit{};
}

// The sum of runs on the plain views, with Lanes lanes (the paths).
template <class T, int Lanes>
[[nodiscard]] Result<Unit, ProductError> sum_strided_runs_by_tree_(StridedView<T, 1> const& out,
                                                                   StridedView<T const, 2> const& runs) noexcept {
    auto const& out_lanes = strided_lanes_of_(out);
    auto const& run_lanes = strided_lanes_of_(runs);
    std::size_t const outputs = out_lanes.sizes[0];
    std::size_t const count = run_lanes.sizes[0];
    if (run_lanes.sizes[1] != outputs) return err(ProductError::ShapeMismatch);
    if (outputs == 0) return Unit{};
    if (count != 0 && shares_elements_(out, runs)) return err(ProductError::OutputOverlapsInput);
    T* const target = strided_first_of_(out);
    std::size_t const out_step = out_lanes.strides[0];
    if (count == 0) {
        for (std::size_t output = 0; output < outputs; ++output) {
            target[output * out_step] = T{};
        }
        return Unit{};
    }
    T const* const first = strided_first_of_(runs);
    std::size_t const run_step = run_lanes.strides[0];
    std::size_t const element_step = run_lanes.strides[1];
    if (count == 1 || run_step == 1) {
        for (std::size_t output = 0; output < outputs; ++output) {
            target[output * out_step] =
                fp::canonicalize_nan(sum_run_by_tree_<T, Lanes>(first + output * element_step, count));
        }
        return Unit{};
    }
    if (outputs == 1 || (element_step == 1 && out_step == 1)) {
        auto const make_leaves = [&]<LaneFill Fill>(std::size_t position, std::size_t active) noexcept {
            return RunLaneLeaves<T, Lanes, Fill>{first + position, run_step, active};
        };
        write_rows_of_lanes_<T, Lanes>(target, outputs, count, make_leaves);
        return Unit{};
    }
    for (std::size_t output = 0; output < outputs; ++output) {
        target[output * out_step] =
            fp::canonicalize_nan(tree_of_leaves_<T>(RunLeaves<T>{first + output * element_step, run_step}, 0, count));
    }
    return Unit{};
}

}  // namespace CRUCIBLE_SIMD_TARGET

}  // namespace detail

inline namespace CRUCIBLE_SIMD_TARGET {

// The tree sum of the products left[i] * right[i] with Lanes lanes in each
// vector, by the order.  The two arguments can swap with no change of a bit.
// Two Views of different counts give LengthMismatch, and an empty View gives
// +0.  Complexity: O(n).
template <int Lanes, class T>
    requires TreeSumElement<T> && detail::TreeLanes<Lanes>
[[nodiscard]] Result<T, LengthMismatch> sum_products_by_tree_in_lanes(View<T const> left,
                                                                      View<T const> right) noexcept {
    if (left.size() != right.size()) return err(LengthMismatch{});
    if (left.size() == 0) return T{};
    return fp::canonicalize_nan(detail::product_run_by_tree_<T, Lanes>(
        first_of_(detail::BaseLayerRead{}, left), first_of_(detail::BaseLayerRead{}, right), left.size()));
}

// The tree sum of the products with the lanes of one register of the target.
[[nodiscard]] inline Result<float, LengthMismatch> sum_products_by_tree(View<float const> left,
                                                                        View<float const> right) noexcept {
    return sum_products_by_tree_in_lanes<simd::native_lanes<float>>(left, right);
}

[[nodiscard]] inline Result<double, LengthMismatch> sum_products_by_tree(View<double const> left,
                                                                         View<double const> right) noexcept {
    return sum_products_by_tree_in_lanes<simd::native_lanes<double>>(left, right);
}

// The matrix product out = left * right with Lanes lanes in each vector, by
// The order and the paths.  The sizes give ShapeMismatch when they do not
// agree, and an output that shares an element with a factor gives
// OutputOverlapsInput.  Each error comes before the first write.  The
// factors come by reference, so a call with the roles compiles to the
// instructions of a call with the plain views.  Complexity: O(M * N * K).
template <int Lanes, class T, std::size_t Rank, class Brand>
    requires TreeSumElement<T> && detail::TreeLanes<Lanes> && (Rank == 2)
[[nodiscard]] Result<Unit, ProductError>
multiply_by_tree_in_lanes(StridedViewMut<T, Rank, Brand>& out, std::type_identity_t<LeftFactorOf<T>> const& left,
                          std::type_identity_t<RightFactorOf<T>> const& right) noexcept {
    return detail::multiply_by_tree_<T, Lanes>(out.view(), left.value(), right.value());
}

// The matrix product with the lanes of one register of the target.
template <class T, std::size_t Rank, class Brand>
    requires TreeSumElement<T> && (Rank == 2)
[[nodiscard]] Result<Unit, ProductError>
multiply_by_tree(StridedViewMut<T, Rank, Brand>& out, std::type_identity_t<LeftFactorOf<T>> const& left,
                 std::type_identity_t<RightFactorOf<T>> const& right) noexcept {
    return detail::multiply_by_tree_<T, simd::native_lanes<T>>(out.view(), left.value(), right.value());
}

// The tree sum of the runs of runs, element by element, with Lanes lanes in
// each vector, by the order and the paths.  The count of the outputs must be
// the size of a run, else the call gives ShapeMismatch.  An output that shares
// an element with the runs gives OutputOverlapsInput.  No run gives +0 at each
// output.  Complexity: O(R * N).
template <int Lanes, class T, std::size_t Rank, class Brand>
    requires TreeSumElement<T> && detail::TreeLanes<Lanes> && (Rank == 1)
[[nodiscard]] Result<Unit, ProductError>
sum_strided_runs_by_tree_in_lanes(StridedViewMut<T, Rank, Brand>& out,
                                  std::type_identity_t<StridedView<T const, 2>> runs) noexcept {
    return detail::sum_strided_runs_by_tree_<T, Lanes>(out.view(), runs);
}

// The sum of runs with the lanes of one register of the target.
template <class T, std::size_t Rank, class Brand>
    requires TreeSumElement<T> && (Rank == 1)
[[nodiscard]] Result<Unit, ProductError>
sum_strided_runs_by_tree(StridedViewMut<T, Rank, Brand>& out,
                         std::type_identity_t<StridedView<T const, 2>> runs) noexcept {
    return sum_strided_runs_by_tree_in_lanes<simd::native_lanes<T>>(out, runs);
}

}  // namespace CRUCIBLE_SIMD_TARGET

}  // namespace foundation::core
