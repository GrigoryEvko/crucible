#pragma once

// The tree reduce of the Region family: a reduce of a View in one stated
// order.  The result has the same bits for each thread count, each schedule,
// each count of SIMD lanes and each host.
//
//   tree_reduce(view, combine)   the tree of the elements with a combine of
//                                the caller, or no value for an empty View
//   tree_sum(view)               the tree sum of a View of float or double,
//                                with SIMD lanes.  An empty View gives +0
//   tree_sum_lanes<L>(view)      the same sum with L lanes, for a test of the
//                                rule that the lanes change no bit
//   tree_sum_of_runs(runs, sums) the tree sum of K runs of one count, element
//                                by element: sums[i] is the tree of run 0 at
//                                i, run 1 at i, and so on, in run order
//   segmented_tree_sum(view, offsets, sums)
//   segmented_tree_reduce(view, offsets, results, empty, combine)
//                                the tree of each segment [offsets[s],
//                                offsets[s + 1]) of the elements
//   check_segment_offsets(n, offsets, S)
//                                the check of the offsets of S segments
//   tree_grain_count<G>(n), tree_sum_grains<G>(...), tree_reduce_grains<G>(...)
//                                the split of a run into grains of G
//                                elements, for a parallel reduce: the tree of
//                                the grain results is the tree of the run
//   tree_depth(n)                the depth of the tree of n elements, which
//                                gives the error bound
//   write_tree_plan(steps), tree_plan<N>(), evaluate_tree_plan(...)
//                                the tree as a list of combine steps, and an
//                                evaluation of such a list
//
// THE ORDER
//   The tree of a run of n elements splits the run at p, the largest power of
//   two below n.  The left part holds the elements [0, p), and the right part
//   holds the elements [p, n).  Each part splits by the same rule, down to one
//   element.  Each node combines the result of its left part, as the left
//   operand, with the result of its right part.  The tree of 3 elements is
//   (x0 + x1) + x2, and the tree of 6 elements is
//   ((x0 + x1) + (x2 + x3)) + (x4 + x5).
//
//   The same tree in a second form: write n as a sum of different powers of
//   two, from the largest.  The run divides into blocks of these sizes, in
//   that order.  Each block is a full balanced tree, and the results of the
//   blocks combine from the right: B1 + (B2 + (... + Br)).
//
//   Each aligned block [k * 2^j, (k + 1) * 2^j) inside the run is a node of
//   the tree, and so is the last part [k * 2^j, n) of the run.  So a split of
//   the run into grains of a power of two splits the tree at its nodes, and
//   the tree of the results of the grains is the tree of the same rule over
//   the grains.  A thread count, a schedule and a grain change no operand of
//   any combine.
//
// THE LANES
//   tree_sum loads L consecutive elements into one vector, with L the count
//   of lanes of one register of the target (foundation/Simd.h).  A merge of
//   two vectors adds the even lanes and the odd lanes of the two
//   (simd::even_lanes, simd::odd_lanes): it forms the sums of the adjacent
//   pairs of 2L consecutive subtrees, and each such sum is a node of the tree
//   above.  The lower subtree is the left operand of each lane.  The last
//   vector of a block reduces its lanes with the same pairs.  So the lanes
//   compute nodes of the tree and no other sum, and 4, 8 or 16 lanes give the
//   same bits.  A test compares each count of lanes with the scalar tree.
//
// THE ERROR BOUND
//   The tree has the depth d = tree_depth(n) = ceil(log2 n).  With the round
//   to nearest of IEEE 754, the computed sum s' of the elements x_i holds
//   |s' - s| <= gamma(d) * sum |x_i|, with gamma(d) = d * u / (1 - d * u), and
//   the unit roundoff u = 2^-24 for float and 2^-53 for double (Higham,
//   Accuracy and Stability of Numerical Algorithms, second edition, section
//   4.2).  The bound needs d * u < 1, which each count of a View gives, and no
//   partial sum that overflows.  An add with a subnormal result is exact, so
//   the bound needs no term for an underflow.  tree_sum_of_runs has the depth
//   tree_depth(K) at each element, and a segment has the depth of its count.
//
// THE VALUES OUTSIDE THE BOUND
//   A NaN element or an overflow gives a NaN or an infinity.  An infinity has
//   the same bits on each host, but a NaN does not.  x86 keeps the payload of
//   one operand and gives 0xFFC00000 for inf + -inf, aarch64 gives its default
//   NaN 0x7FC00000, and GCC can swap the operands of an add.  So each output
//   of a sum form that is a NaN is the canonical NaN quiet_nan_of<T> of
//   foundation/core/Number.h: 0x7FC00000 for a float, 0x7FF8000000000000 for
//   a double.  The rule costs one compare and one select at each output, also
//   at an output of one element.  Each other output keeps the bits of its
//   adds.  The forms with the combine of a caller give the bits of the
//   combine, so the caller states the rule of its combine.  A select or a
//   copy of the base keeps the bits of its value.
//
// THE COSTS
//   tree_reduce makes one combine for each element after the first, with a
//   recursion of depth ceil(log2 n) + popcount(n).  tree_sum makes one load
//   and about two shuffles and one add for each vector.  No operation
//   allocates, and each split form takes its partials from the caller.
//
// THE TARGET
//   tree_sum, tree_sum_lanes, tree_sum_of_runs, segmented_tree_sum and
//   tree_sum_grains read the vector width of the target.  So they and their
//   SIMD helpers are in the inline namespace CRUCIBLE_SIMD_TARGET (THE TARGET
//   NAMESPACE of foundation/Platform.h).  The forms with the combine of a
//   caller, the types and the plans read no target, and they stay outside it.

#include <foundation/Simd.h>
#include <foundation/core/Array.h>
#include <foundation/core/Bits.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Float.h>
#include <foundation/core/Number.h>
#include <foundation/core/Region.h>
#include <foundation/core/Report.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace foundation::core {

// A value of a tree: an object that a combine copies, with no throw.
template <class T>
concept TreeValue = std::is_object_v<T> && !std::is_array_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T>
                 && std::is_nothrow_copy_constructible_v<T> && std::is_nothrow_move_constructible_v<T>
                 && std::is_nothrow_destructible_v<T>;

// A combine of a tree: it takes the left operand and the right operand, gives
// a value of the same type, and does not throw.
template <class Combine, class T>
concept TreeCombine = std::is_nothrow_invocable_v<Combine&, T const&, T const&>
                   && std::same_as<std::invoke_result_t<Combine&, T const&, T const&>, T>;

// An element of tree_sum: a float or a double.
template <class T>
concept TreeSumElement = std::same_as<T, float> || std::same_as<T, double>;

// A grain of a split: a power of two of elements.
template <std::size_t Grain>
concept TreeGrain = has_single_bit(Grain);

// The depth of the tree of count elements: ceil(log2 count), and 0 for 0 or
// 1 element.  The error bound of a sum reads it.
[[nodiscard]] constexpr int tree_depth(std::size_t count) noexcept { return count <= 1 ? 0 : bit_width(count - 1); }

namespace detail {

// The output of a sum by THE VALUES OUTSIDE THE BOUND: the canonical NaN for
// a NaN, and value for each other value.  One compare and one select.
template <class T>
[[nodiscard]] CRUCIBLE_INLINE constexpr T canonical_sum_(T value) noexcept {
    return fp::is_nan(value) ? quiet_nan_of<T> : value;
}

// The add of a tree sum.  The left operand is the result of the lower
// elements.
struct TreeAdd {
    template <class T>
    [[nodiscard]] constexpr T operator()(T const& left, T const& right) const noexcept {
        return left + right;
    }
};

// The tree of the count elements from first, by the rule of THE ORDER.  The
// count is at least 1.  A run of up to four elements has its tree inline,
// so the recursion makes about one call for each two elements.
template <class T, class Combine>
[[nodiscard]] constexpr T tree_of_run_(T const* first, std::size_t count, Combine& combine) noexcept {
    switch (count) {
        case 1:
            return first[0];
        case 2:
            return combine(first[0], first[1]);
        case 3: {
            T const pair = combine(first[0], first[1]);
            return combine(pair, first[2]);
        }
        case 4: {
            T const low = combine(first[0], first[1]);
            T const high = combine(first[2], first[3]);
            return combine(low, high);
        }
        default:
            break;
    }
    std::size_t const split = bit_floor(count - 1);
    T const left = tree_of_run_(first, split, combine);
    T const right = tree_of_run_(first + split, count - split, combine);
    return combine(left, right);
}

inline namespace CRUCIBLE_SIMD_TARGET {

// The outputs of the lanes by THE VALUES OUTSIDE THE BOUND: the canonical NaN
// in each lane that holds a NaN.  A lane holds a NaN when its bits with no
// sign are above the bits of an infinity.  The test and the select work on
// the bits as integers, so they need no compare of floats.  One compare and
// one select for each vector.
template <class T, int Lanes>
[[nodiscard]] CRUCIBLE_INLINE simd::vec<T, Lanes> canonical_sums_(simd::vec<T, Lanes> sums) noexcept {
    using Word = typename FloatBits<T>::Word;
    using Words = simd::vec<Word, Lanes>;
    Words const bits = simd::bit_cast<Words>(sums);
    Words const magnitudes = bits & Words{static_cast<Word>(static_cast<Word>(~Word{0}) >> 1)};
    Words const canonical =
        simd::select<Word, Lanes>(magnitudes > Words{FloatBits<T>::infinity}, Words{FloatBits<T>::quiet_nan}, bits);
    return simd::bit_cast<simd::vec<T, Lanes>>(canonical);
}

// The merge of two vectors of Lanes consecutive subtrees each: the sums of
// the adjacent pairs of the 2 * Lanes subtrees, in order.
template <class T, int Lanes>
[[nodiscard]] CRUCIBLE_INLINE simd::vec<T, Lanes> merge_tree_vectors_(simd::vec<T, Lanes> low,
                                                                      simd::vec<T, Lanes> high) noexcept {
    return simd::even_lanes(low, high) + simd::odd_lanes(low, high);
}

// The tree of the lanes of one vector: the adjacent pairs, then the pairs of
// the pairs, down to one value.
template <class T, int Lanes>
[[nodiscard]] CRUCIBLE_INLINE T reduce_tree_lanes_(simd::vec<T, Lanes> lanes) noexcept {
    if constexpr (Lanes == 2) {
        return lanes[0] + lanes[1];
    } else {
        return reduce_tree_lanes_<T, Lanes / 2>(simd::even_lanes(lanes) + simd::odd_lanes(lanes));
    }
}

// The vector of a block of Vectors vectors from first, a power of two of at
// most 16.  Lane i holds the tree of the Vectors elements of the block that
// start at element i * Vectors.  The recursion is a template, so the block is
// one straight run of loads, shuffles and adds.
template <class T, int Lanes, std::size_t Vectors>
[[nodiscard]] CRUCIBLE_INLINE simd::vec<T, Lanes> tree_block_fixed_(T const* first) noexcept {
    if constexpr (Vectors == 1) {
        return simd::load<simd::vec<T, Lanes>>(first);
    } else {
        constexpr std::size_t half = Vectors / 2;
        simd::vec<T, Lanes> const low = tree_block_fixed_<T, Lanes, half>(first);
        simd::vec<T, Lanes> const high = tree_block_fixed_<T, Lanes, half>(first + half * Lanes);
        return merge_tree_vectors_<T, Lanes>(low, high);
    }
}

// The vector of a block of vectors vectors from first, a power of two.  Lane
// i holds the tree of the vectors elements that start at element i * vectors.
// A block of more than 16 vectors is the merge of its two halves.
template <class T, int Lanes>
[[nodiscard]] simd::vec<T, Lanes> tree_block_vector_(T const* first, std::size_t vectors) noexcept {
    switch (vectors) {
        case 1:
            return tree_block_fixed_<T, Lanes, 1>(first);
        case 2:
            return tree_block_fixed_<T, Lanes, 2>(first);
        case 4:
            return tree_block_fixed_<T, Lanes, 4>(first);
        case 8:
            return tree_block_fixed_<T, Lanes, 8>(first);
        case 16:
            return tree_block_fixed_<T, Lanes, 16>(first);
        default:
            break;
    }
    std::size_t const half = vectors / 2;
    simd::vec<T, Lanes> const low = tree_block_vector_<T, Lanes>(first, half);
    simd::vec<T, Lanes> const high = tree_block_vector_<T, Lanes>(first + half * Lanes, half);
    return merge_tree_vectors_<T, Lanes>(low, high);
}

// The tree sum of the count elements from first, with vectors of Lanes
// lanes.  The count is at least 1.  A part of fewer than Lanes elements takes
// the scalar tree.  A part of a power of two takes a block of vectors.  Each
// other part splits by the rule of THE ORDER, so the recursion follows the
// right edge of the tree, with a depth of at most popcount(count).
template <class T, int Lanes>
[[nodiscard]] T tree_sum_run_(T const* first, std::size_t count) noexcept {
    constexpr auto lanes = static_cast<std::size_t>(Lanes);
    TreeAdd add{};
    if (count < lanes) return tree_of_run_(first, count, add);
    if (has_single_bit(count)) return reduce_tree_lanes_<T, Lanes>(tree_block_vector_<T, Lanes>(first, count / lanes));
    std::size_t const split = bit_floor(count - 1);
    T const left = tree_sum_run_<T, Lanes>(first, split);
    T const right = tree_sum_run_<T, Lanes>(first + split, count - split);
    return left + right;
}

}  // namespace CRUCIBLE_SIMD_TARGET

// The count of the lanes of a test of tree_sum_lanes: a power of two from 2
// to 64.
template <int Lanes>
concept TreeLanes = Lanes >= 2 && Lanes <= 64 && has_single_bit(static_cast<unsigned>(Lanes));

}  // namespace detail

// The tree of the elements, by the rule of THE ORDER, with the combine of
// the caller, or no value for an empty View.  The combine runs one time for
// each element after the first.  Complexity: O(n) combines.
template <class T, std::size_t Extent, class Combine>
    requires TreeValue<std::remove_const_t<T>> && TreeCombine<Combine, std::remove_const_t<T>>
[[nodiscard]] constexpr Option<std::remove_const_t<T>> tree_reduce(View<T, Extent> elements, Combine combine) noexcept {
    using Value = std::remove_const_t<T>;
    if (elements.size() == 0) return none;
    Value const* const first = first_of_(detail::ViewDoor::key(), elements);
    return Option<Value>::some(detail::tree_of_run_(first, elements.size(), combine));
}

inline namespace CRUCIBLE_SIMD_TARGET {

// The tree sum of the elements with Lanes lanes in each vector.  The count
// of lanes changes no bit (THE LANES), so a test compares each count.  An
// empty View gives +0.  Complexity: O(n).
template <int Lanes, class T>
    requires TreeSumElement<T> && detail::TreeLanes<Lanes>
[[nodiscard]] T tree_sum_lanes(View<T const> elements) noexcept {
    if (elements.size() == 0) return T{};
    return detail::canonical_sum_(
        detail::tree_sum_run_<T, Lanes>(first_of_(detail::ViewDoor::key(), elements), elements.size()));
}

// The tree sum of the elements, with the lanes of one register of the
// target.  An empty View gives +0.  Complexity: O(n).
[[nodiscard]] inline float tree_sum(View<float const> elements) noexcept {
    return tree_sum_lanes<simd::native_lanes<float>>(elements);
}

[[nodiscard]] inline double tree_sum(View<double const> elements) noexcept {
    return tree_sum_lanes<simd::native_lanes<double>>(elements);
}

}  // namespace CRUCIBLE_SIMD_TARGET

// ── The sum of runs, element by element ─────────────────────────────────

namespace detail {

inline namespace CRUCIBLE_SIMD_TARGET {

// The tree over the runs [first, first + count) of the vector at offset of
// each run.  The count is at least 1.
template <class T, int Lanes>
[[nodiscard]] simd::vec<T, Lanes> tree_runs_vector_(View<T const> const* runs, std::size_t count,
                                                    std::size_t offset) noexcept {
    using V = simd::vec<T, Lanes>;
    if (count == 1) return simd::load<V>(first_of_(ViewDoor::key(), runs[0]) + offset);
    std::size_t const split = bit_floor(count - 1);
    V const left = tree_runs_vector_<T, Lanes>(runs, split, offset);
    V const right = tree_runs_vector_<T, Lanes>(runs + split, count - split, offset);
    return left + right;
}

}  // namespace CRUCIBLE_SIMD_TARGET

// The tree over the runs [first, first + count) of the element at offset of
// each run.  The count is at least 1.
template <class T>
[[nodiscard]] T tree_runs_element_(View<T const> const* runs, std::size_t count, std::size_t offset) noexcept {
    if (count == 1) return first_of_(ViewDoor::key(), runs[0])[offset];
    std::size_t const split = bit_floor(count - 1);
    T const left = tree_runs_element_<T>(runs, split, offset);
    T const right = tree_runs_element_<T>(runs + split, count - split, offset);
    return left + right;
}

}  // namespace detail

inline namespace CRUCIBLE_SIMD_TARGET {

// The tree sum of the runs element by element: sums[i] is the tree of the
// elements at i of the runs, in run order, by the rule of THE ORDER.  So a
// sum of K producers into one result has a fixed topology, and the order in
// which the producers finish changes no bit.  Each run must hold the count of
// sums, and a run of a different count gives LengthMismatch with no write.
// No run gives +0 in each sum.  Complexity: O(K * n) loads and adds, with a
// recursion of depth ceil(log2 K) for each vector of sums.
template <class T>
    requires TreeSumElement<T>
[[nodiscard]] Result<Unit, LengthMismatch> tree_sum_of_runs(View<View<T const> const> runs, View<T> sums) noexcept {
    constexpr int lanes = simd::native_lanes<T>;
    std::size_t const count = sums.size();
    for (View<T const> const run : runs) {
        if (run.size() != count) return err(LengthMismatch{});
    }
    T* const out = first_of_(detail::ViewDoor::key(), sums);
    if (runs.size() == 0) {
        fill(sums, T{});
        return Unit{};
    }
    View<T const> const* const first_run = first_of_(detail::ViewDoor::key(), runs);
    std::size_t const whole = count - count % static_cast<std::size_t>(lanes);
    for (std::size_t offset = 0; offset < whole; offset += static_cast<std::size_t>(lanes)) {
        simd::store(
            detail::canonical_sums_<T, lanes>(detail::tree_runs_vector_<T, lanes>(first_run, runs.size(), offset)),
            out + offset);
    }
    for (std::size_t offset = whole; offset < count; ++offset) {
        out[offset] = detail::canonical_sum_(detail::tree_runs_element_<T>(first_run, runs.size(), offset));
    }
    return Unit{};
}

}  // namespace CRUCIBLE_SIMD_TARGET

// ── The segments ────────────────────────────────────────────────────────

// The error of a list of the offsets of segments.
enum class SegmentError : std::uint8_t {
    // The offsets do not hold one more element than the results.
    CountMismatch = 0,
    // An offset is below the offset before it.
    OffsetsOutOfOrder = 1,
    // The last offset is past the count of the elements.
    OffsetPastEnd = 2,
};

// The check of the offsets of results segments of a run of elements
// elements: the offsets hold one more element than the results, they do not
// decrease, and the last one is at most the count of the elements.  A
// parallel caller checks the offsets with it before the split.  A window of
// checked offsets passes the check too.  Complexity: O(S).
[[nodiscard]] constexpr Result<Unit, SegmentError>
check_segment_offsets(std::size_t elements, View<std::size_t const> offsets, std::size_t results) noexcept {
    if (offsets.size() != results + 1) return err(SegmentError::CountMismatch);
    std::size_t const* const offset = first_of_(detail::ViewDoor::key(), offsets);
    for (std::size_t segment = 0; segment < results; ++segment) {
        if (offset[segment + 1] < offset[segment]) return err(SegmentError::OffsetsOutOfOrder);
    }
    if (offset[results] > elements) return err(SegmentError::OffsetPastEnd);
    return Unit{};
}

inline namespace CRUCIBLE_SIMD_TARGET {

// The tree sum of each segment: sums[s] is the tree sum of the elements
// [offsets[s], offsets[s + 1]), and an empty segment gives +0.  The offsets
// hold one more element than the sums, they do not decrease, and the last one
// is at most the count of the elements.  Else the call gives a SegmentError
// and writes nothing.  Complexity: O(n + S).
template <class T>
    requires TreeSumElement<T>
[[nodiscard]] Result<Unit, SegmentError> segmented_tree_sum(View<T const> elements, View<std::size_t const> offsets,
                                                            View<T> sums) noexcept {
    auto const checked = check_segment_offsets(elements.size(), offsets, sums.size());
    if (checked.is_err()) return checked;
    T const* const first = first_of_(detail::ViewDoor::key(), elements);
    std::size_t const* const offset = first_of_(detail::ViewDoor::key(), offsets);
    T* const out = first_of_(detail::ViewDoor::key(), sums);
    for (std::size_t segment = 0; segment < sums.size(); ++segment) {
        std::size_t const count = offset[segment + 1] - offset[segment];
        out[segment] = count == 0 ? T{}
                                  : detail::canonical_sum_(detail::tree_sum_run_<T, simd::native_lanes<T>>(
                                        first + offset[segment], count));
    }
    return Unit{};
}

}  // namespace CRUCIBLE_SIMD_TARGET

// The tree of each segment with the combine of the caller: results[s] is the
// tree of the elements [offsets[s], offsets[s + 1]), and an empty segment
// gives empty.  The offsets obey the rules of segmented_tree_sum.
// Complexity: O(n + S) combines.
template <class T, class Combine>
    requires TreeValue<T> && TreeCombine<Combine, T> && std::is_nothrow_copy_assignable_v<T>
[[nodiscard]] constexpr Result<Unit, SegmentError>
segmented_tree_reduce(View<T const> elements, View<std::size_t const> offsets, View<T> results, T const& empty,
                      Combine combine) noexcept {
    auto const checked = check_segment_offsets(elements.size(), offsets, results.size());
    if (checked.is_err()) return checked;
    T const* const first = first_of_(detail::ViewDoor::key(), elements);
    std::size_t const* const offset = first_of_(detail::ViewDoor::key(), offsets);
    T* const out = first_of_(detail::ViewDoor::key(), results);
    for (std::size_t segment = 0; segment < results.size(); ++segment) {
        std::size_t const count = offset[segment + 1] - offset[segment];
        out[segment] = count == 0 ? empty : detail::tree_of_run_(first + offset[segment], count, combine);
    }
    return Unit{};
}

// ── The split into grains ───────────────────────────────────────────────

// The count of the grains of a run of count elements: each grain holds Grain
// elements, and the last grain holds the rest.
template <std::size_t Grain>
    requires TreeGrain<Grain>
[[nodiscard]] constexpr std::size_t tree_grain_count(std::size_t count) noexcept {
    return count / Grain + (count % Grain != 0 ? 1 : 0);
}

namespace detail {

// The end of a range of grains: first_grain + partials, which must be at most
// the count of the grains of the run.
template <std::size_t Grain>
constexpr void check_grain_range_(std::size_t count, std::size_t first_grain, std::size_t partials) noexcept {
    std::size_t const grains = tree_grain_count<Grain>(count);
    if (first_grain > grains || partials > grains - first_grain) [[unlikely]] {
        fatal("a range of {} grains from grain {} is past the {} grains of the run", partials, first_grain, grains);
    }
}

}  // namespace detail

inline namespace CRUCIBLE_SIMD_TARGET {

// The tree sum of each grain of the range: partials[i] is the tree sum of the
// grain first_grain + i of the elements.  The tree sum of all the partials of
// the run is then the tree sum of the run, so tree_sum(partials) gives it.  A
// range past the grains of the run ends the process.  Complexity: O(Grain)
// for each partial.
template <std::size_t Grain, class T>
    requires TreeGrain<Grain> && TreeSumElement<T>
void tree_sum_grains(View<T const> elements, std::size_t first_grain, View<T> partials) noexcept {
    std::size_t const count = elements.size();
    detail::check_grain_range_<Grain>(count, first_grain, partials.size());
    T const* const first = first_of_(detail::ViewDoor::key(), elements);
    T* const out = first_of_(detail::ViewDoor::key(), partials);
    for (std::size_t index = 0; index < partials.size(); ++index) {
        std::size_t const start = (first_grain + index) * Grain;
        std::size_t const size = count - start < Grain ? count - start : Grain;
        out[index] = detail::canonical_sum_(detail::tree_sum_run_<T, simd::native_lanes<T>>(first + start, size));
    }
}

}  // namespace CRUCIBLE_SIMD_TARGET

// The tree of each grain of the range with the combine of the caller:
// partials[i] is the tree of the grain first_grain + i.  tree_reduce of all
// the partials of the run then gives the tree of the run.  A range past the
// grains of the run ends the process.
template <std::size_t Grain, class T, class Combine>
    requires TreeGrain<Grain> && TreeValue<T> && TreeCombine<Combine, T> && std::is_nothrow_copy_assignable_v<T>
constexpr void tree_reduce_grains(View<T const> elements, std::size_t first_grain, View<T> partials,
                                  Combine combine) noexcept {
    std::size_t const count = elements.size();
    detail::check_grain_range_<Grain>(count, first_grain, partials.size());
    T const* const first = first_of_(detail::ViewDoor::key(), elements);
    T* const out = first_of_(detail::ViewDoor::key(), partials);
    for (std::size_t index = 0; index < partials.size(); ++index) {
        std::size_t const start = (first_grain + index) * Grain;
        std::size_t const size = count - start < Grain ? count - start : Grain;
        out[index] = detail::tree_of_run_(first + start, size, combine);
    }
}

// ── The plan of the tree ────────────────────────────────────────────────

// A node of a plan: leaf i is the node i, and step s of a plan of n leaves
// gives the node n + s.
enum class PlanNode : std::uint32_t {
};

// One combine of a plan: the node of the left operand and the node of the
// right operand.
struct TreeStep {
    PlanNode left{};
    PlanNode right{};

    [[nodiscard]] friend constexpr bool operator==(TreeStep const&, TreeStep const&) noexcept = default;
};

// The error of a plan.
enum class PlanError : std::uint8_t {
    // The counts of the steps, the leaves and the scratch do not agree.
    CountMismatch = 0,
    // A step reads a node that no earlier step or leaf gives.
    NodeNotReady = 1,
    // The plan has more leaves than a PlanNode can number.
    TooManyLeaves = 2,
};

// The largest count of the leaves of a plan: each node of a plan of n leaves
// is below 2n - 1, so the nodes fit a 32-bit PlanNode.
inline constexpr std::size_t max_plan_leaves = std::size_t{1} << 31;

namespace detail {

// Writes the steps of the tree of the count leaves from first_leaf, in post
// order, from the step next, and gives the node of the root.  A step comes
// after the steps of its two operands.
[[nodiscard]] constexpr PlanNode write_tree_plan_(TreeStep* steps, std::uint32_t leaves, std::uint32_t first_leaf,
                                                  std::uint32_t count, std::uint32_t& next) noexcept {
    if (count == 1) return PlanNode{first_leaf};
    std::uint32_t const split = bit_floor(count - 1u);
    PlanNode const left = write_tree_plan_(steps, leaves, first_leaf, split, next);
    PlanNode const right = write_tree_plan_(steps, leaves, first_leaf + split, count - split, next);
    steps[next] = TreeStep{left, right};
    PlanNode const node{leaves + next};
    ++next;
    return node;
}

}  // namespace detail

// Writes the tree of steps.size() + 1 leaves, by the rule of THE ORDER, as
// steps.size() combine steps.  Each step comes after the steps of its
// operands, and the last step gives the root.  A net check reads the plan as
// the specification of the order.  A plan of more than max_plan_leaves
// leaves gives TooManyLeaves and writes nothing.  Complexity: O(n).
template <std::size_t Extent>
[[nodiscard]] constexpr Result<Unit, PlanError> write_tree_plan(View<TreeStep, Extent> steps) noexcept {
    if (steps.size() >= max_plan_leaves) return err(PlanError::TooManyLeaves);
    auto const leaves = static_cast<std::uint32_t>(steps.size() + 1);
    std::uint32_t next = 0;
    static_cast<void>(detail::write_tree_plan_(first_of_(detail::ViewDoor::key(), steps), leaves, 0, leaves, next));
    return Unit{};
}

// The plan of the tree of Leaves leaves, as a constant.
template <std::size_t Leaves>
    requires(Leaves >= 2 && Leaves <= 4096)
[[nodiscard]] consteval FixedArray<TreeStep, Leaves - 1> tree_plan() noexcept {
    FixedArray<TreeStep, Leaves - 1> steps{};
    static_cast<void>(write_tree_plan(steps.view()).expect("a plan of at most 4096 leaves fits a PlanNode"));
    return steps;
}

// The value of a plan over the leaves, with the combine of the caller.  The
// scratch holds the value of each step.  The leaves hold one more element than
// the steps, and the scratch holds as many as the steps.  A step that reads a
// node of a later step gives NodeNotReady.  Complexity: O(steps) combines.
template <class T, class Combine>
    requires TreeValue<T> && TreeCombine<Combine, T> && std::is_nothrow_copy_assignable_v<T>
[[nodiscard]] constexpr Result<T, PlanError> evaluate_tree_plan(View<TreeStep const> steps, View<T const> leaves,
                                                                View<T> scratch, Combine combine) noexcept {
    std::size_t const count = steps.size();
    if (leaves.size() != count + 1 || scratch.size() != count) return err(PlanError::CountMismatch);
    TreeStep const* const step = first_of_(detail::ViewDoor::key(), steps);
    T const* const leaf = first_of_(detail::ViewDoor::key(), leaves);
    T* const value = first_of_(detail::ViewDoor::key(), scratch);
    std::size_t const leaf_count = count + 1;
    for (std::size_t index = 0; index < count; ++index) {
        auto const left = static_cast<std::size_t>(step[index].left);
        auto const right = static_cast<std::size_t>(step[index].right);
        if (left >= leaf_count + index || right >= leaf_count + index) return err(PlanError::NodeNotReady);
        T const& left_value = left < leaf_count ? leaf[left] : value[left - leaf_count];
        T const& right_value = right < leaf_count ? leaf[right] : value[right - leaf_count];
        value[index] = combine(left_value, right_value);
    }
    return count == 0 ? leaf[0] : value[count - 1];
}

}  // namespace foundation::core
