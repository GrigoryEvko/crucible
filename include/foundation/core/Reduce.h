#pragma once

// The tree reduce of the Region family: a reduce of a View in one stated
// order.  The result has the same bits for each thread count, each schedule,
// each count of SIMD lanes and each host.
//
//   reduce_by_tree(view, combine)
//                                the tree of the elements with a combine of
//                                the caller, or no value for an empty View
//   sum_by_tree(view)            the tree sum of a View of float or double,
//                                with SIMD lanes.  An empty View gives +0
//   sum_by_tree_in_lanes<L>(view)
//                                the same sum with L lanes, for a test of the
//                                rule that the lanes change no bit
//   sum_runs_by_tree(runs, sums) the tree sum of K runs of one count, element
//                                by element: sums[i] is the tree of run 0 at
//                                i, run 1 at i, and so on, in run order.  The
//                                sums can be the View of a run, and no other
//                                overlap of a run and the sums is permitted
//   sum_segments_by_tree(view, offsets, sums)
//   reduce_segments_by_tree(view, offsets, results, empty, combine)
//                                the tree of each segment [offsets[s],
//                                offsets[s + 1]) of the elements
//   check_segment_offsets(n, offsets, S)
//                                the check of the offsets of S segments
//   count_tree_grains<G>(n), sum_grains_by_tree<G>(...),
//   reduce_grains_by_tree<G>(...)
//                                the split of a run into grains of G
//                                elements, for a parallel reduce: the tree of
//                                the grain results is the tree of the run
//   count_tree_depth(n)          the depth of the tree of n elements, which
//                                gives the error bound
//   write_tree_plan(steps), plan_tree<N>(), evaluate_tree_plan(...)
//                                the tree as a list of combine steps, and an
//                                evaluation of such a list
//
// Each output is a ViewMut of a frame door (foundation/core/Region.h), which
// the call takes by value, so the output has one writer by its type.  The
// inputs are Views.
//
// Order
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
// Lanes
//   sum_by_tree loads L consecutive elements into one vector, with L the count
//   of lanes of one register of the target (foundation/Simd.h).  A merge of
//   two vectors adds the even lanes and the odd lanes of the two
//   (simd::pick_even_lanes, simd::pick_odd_lanes): it forms the sums of the adjacent
//   pairs of 2L consecutive subtrees, and each such sum is a node of the tree
//   above.  The lower subtree is the left operand of each lane.  The last
//   vector of a block reduces its lanes with the same pairs.  So the lanes
//   compute nodes of the tree and no other sum, and 4, 8 or 16 lanes give the
//   same bits.  A test compares each count of lanes with the scalar tree.
//
// Error bound
//   The tree has the depth d = count_tree_depth(n) = ceil(log2 n).  With the round
//   to nearest of IEEE 754, the computed sum s' of the elements x_i holds
//   |s' - s| <= gamma(d) * sum |x_i|, with gamma(d) = d * u / (1 - d * u), and
//   the unit roundoff u = 2^-24 for float and 2^-53 for double (Higham,
//   Accuracy and Stability of Numerical Algorithms, second edition, section
//   4.2).  The bound needs d * u < 1, which each count of a View gives, and no
//   partial sum that overflows.  An add with a subnormal result is exact, so
//   the bound needs no term for an underflow.  sum_runs_by_tree has the depth
//   count_tree_depth(K) at each element, and a segment has the depth of its count.
//
// Values outside the bound
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
// Costs
//   reduce_by_tree makes one combine for each element after the first, with a
//   recursion of depth ceil(log2 n) + popcount(n).  For a value that copies as
//   bytes and has a default value (TreeLeafValue), each aligned block of a
//   power of two of at most 256 elements is a leaf: a pass over a scratch
//   array, level by level, with no call.  A long run then makes about one
//   call for each 128 elements, and the loops of a plain combine are vector
//   loops.  Each other value takes the recursion, with about one call for
//   each two elements.  sum_by_tree makes one load and about two shuffles and
//   one add for each vector.  No operation allocates, and each split form
//   takes its partials from the caller.
//
// Calls of a combine
//   The tree states the two operands of each combine, and not the sequence of
//   the calls: a leaf makes the combines of one level before the next level.
//   So a combine gives a value that depends only on its two operands, and a
//   state of the combine must not change its result.
//
// Target
//   sum_by_tree, sum_by_tree_in_lanes, sum_runs_by_tree, sum_segments_by_tree and
//   sum_grains_by_tree read the vector width of the target.  So they and their
//   SIMD helpers are in the inline namespace CRUCIBLE_SIMD_TARGET (the target
//   namespace of foundation/Platform.h).  The forms with the combine of a
//   caller, the types and the plans read no target, and they stay outside it.

#include <foundation/NoObject.h>
#include <foundation/Platform.h>
#include <foundation/Simd.h>
#include <foundation/core/Array.h>
#include <foundation/core/Bits.h>
#include <foundation/core/CaptureStore.h>
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
// a value of the same type, does not throw, and holds no linear value by value
// (HoldsNoLinearValue of foundation/Linear.h).  A door calls the combine many
// times and ends it, and a call is no consume.  The combine stores no borrow
// outside the door (StoresNoBorrowOutside of foundation/core/CaptureStore.h):
// an operand can be a partial value of the tree, which ends in the door.
template <class Combine, class T>
concept TreeCombine = std::is_nothrow_invocable_v<Combine&, T const&, T const&>
                   && std::same_as<std::invoke_result_t<Combine&, T const&, T const&>, T>
                   && ::foundation::linear::HoldsNoLinearValue<Combine> && StoresNoBorrowOutside<Combine>;

// A combine that holds a linear value by value: the deleted twin of each
// door of a tree and of a scan binds to it.
template <class Combine, class T>
concept LinearTreeCombine =
    (!::foundation::linear::HoldsNoLinearValue<Combine>) && std::is_invocable_v<Combine&, T const&, T const&>;

// An element of sum_by_tree: a float or a double.
template <class T>
concept TreeSumElement = std::same_as<T, float> || std::same_as<T, double>;

// A grain of a split: a power of two of elements.
template <std::size_t Grain>
concept TreeGrain = has_single_bit(Grain);

// The depth of the tree of count elements: ceil(log2 count), and 0 for 0 or
// 1 element.  The error bound of a sum reads it.
[[nodiscard]] constexpr int count_tree_depth(std::size_t count) noexcept {
    return count <= 1 ? 0 : bit_width(count - 1);
}

namespace detail {

// The add of a tree sum.  The left operand is the result of the lower
// elements.
struct TreeAdd {
    template <class T>
    [[nodiscard]] constexpr T operator()(T const& left, T const& right) const noexcept {
        return left + right;
    }
};

// The largest leaf of the tree of a combine: a power of two of elements.
inline constexpr std::size_t tree_leaf_count = 256;

// The largest value, in bytes, that the leaves of a combine keep in a scratch
// array: the scratch of the largest leaf then holds at most 12 KB.
inline constexpr std::size_t tree_leaf_value_bytes = 64;

// A value that the leaves of a combine keep in a scratch array: a default
// value, a copy and an assignment of the bytes with no other effect, and at
// most tree_leaf_value_bytes bytes.
template <class T>
concept TreeLeafValue = std::is_trivially_copyable_v<T> && std::is_nothrow_default_constructible_v<T>
                     && std::is_nothrow_copy_assignable_v<T> && sizeof(T) <= tree_leaf_value_bytes;

// One level of a leaf: the Width values of target from the 2 * Width values
// of source, each the combine of an adjacent pair, the lower value as the left
// operand.  The count is a constant, so the loop of a plain combine is a
// vector loop with no tail.
template <std::size_t Width, class T, class Combine>
CRUCIBLE_INLINE constexpr void combine_level_(T const* source, T* target, Combine& combine) noexcept {
    for (std::size_t position = 0; position < Width; ++position) {
        target[position] = combine(source[2 * position], source[2 * position + 1]);
    }
}

// The levels of a leaf from Width values in source, with target for the next
// level: the two scratch arrays take turns, so no level writes a value that it
// reads.
template <std::size_t Width, class T, class Combine>
[[nodiscard]] CRUCIBLE_INLINE constexpr T tree_of_levels_(T* source, T* target, Combine& combine) noexcept {
    if constexpr (Width == 1) {
        return source[0];
    } else {
        combine_level_<Width / 2>(source, target, combine);
        return tree_of_levels_<Width / 2>(target, source, combine);
    }
}

// The balanced tree of the Count elements from first, a power of two from 8
// to tree_leaf_count, level by level.  The first level combines each adjacent
// pair of elements, and each later level combines each adjacent pair of the
// level below it, with the lower value as the left operand.  Each value of a
// level is the node of the order over its aligned block, so the last value is
// the tree of the run.  Count - 1 combines.
template <std::size_t Count, class T, class Combine>
[[nodiscard]] constexpr T tree_of_leaf_(T const* first, Combine& combine) noexcept {
    T low[Count / 2]{};
    T high[Count / 4]{};
    combine_level_<Count / 2>(first, low, combine);
    return tree_of_levels_<Count / 2>(low, high, combine);
}

// The leaves of the tree of a combine.
enum class TreeLeaves : std::uint8_t {
    // Each part of up to four elements has its tree inline: the form for the
    // short runs of the sum forms, which take no leaf.
    Inline,
    // A part of a TreeLeafValue of a power of two from 8 to tree_leaf_count
    // takes the leaf of its count.
    Blocked,
};

// The tree of the count elements from first, by the rule of the order.  The
// count is at least 1.  A run of up to four elements has its tree inline.
// With the Blocked leaves, a part of a TreeLeafValue of a power of two from 8
// to tree_leaf_count takes the leaf of its count, so a long run makes about
// one call for each 128 elements.  Each other run makes about one call for
// each two elements.
template <TreeLeaves Leaves, class T, class Combine>
[[nodiscard]] constexpr T tree_of_run_(T const* first, std::size_t count, Combine& combine) noexcept {
    if constexpr (Leaves == TreeLeaves::Blocked && TreeLeafValue<T>) {
        switch (count) {
            case 8:
                return tree_of_leaf_<8>(first, combine);
            case 16:
                return tree_of_leaf_<16>(first, combine);
            case 32:
                return tree_of_leaf_<32>(first, combine);
            case 64:
                return tree_of_leaf_<64>(first, combine);
            case 128:
                return tree_of_leaf_<128>(first, combine);
            case tree_leaf_count:
                return tree_of_leaf_<tree_leaf_count>(first, combine);
            default:
                break;
        }
    }
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
    T const left = tree_of_run_<Leaves>(first, split, combine);
    T const right = tree_of_run_<Leaves>(first + split, count - split, combine);
    return combine(left, right);
}

inline namespace CRUCIBLE_SIMD_TARGET {

// The merge of two vectors of Lanes consecutive subtrees each: the sums of
// the adjacent pairs of the 2 * Lanes subtrees, in order.
template <class T, int Lanes>
[[nodiscard]] CRUCIBLE_INLINE simd::vec<T, Lanes> merge_tree_vectors_(simd::vec<T, Lanes> low,
                                                                      simd::vec<T, Lanes> high) noexcept {
    return simd::pick_even_lanes(low, high) + simd::pick_odd_lanes(low, high);
}

// The tree of the lanes of one vector: the adjacent pairs, then the pairs of
// the pairs, down to one value.  Each step keeps its pairs in a named vector.
// On aarch64 a vec of more than 16 bytes returns in memory.  If the add builds
// its result in the argument slot of the next step, GCC 16 with UBSan at -O1
// warns that the slot can be uninitialized, which is false.  The named vector
// prevents that warning, and the code at -O3 is the same.
template <class T, int Lanes>
[[nodiscard]] CRUCIBLE_INLINE T reduce_tree_lanes_(simd::vec<T, Lanes> lanes) noexcept {
    if constexpr (Lanes == 2) {
        return lanes[0] + lanes[1];
    } else {
        simd::vec<T, Lanes / 2> const pairs = simd::pick_even_lanes(lanes) + simd::pick_odd_lanes(lanes);
        return reduce_tree_lanes_<T, Lanes / 2>(pairs);
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
// other part splits by the rule of the order, so the recursion follows the
// right edge of the tree, with a depth of at most popcount(count).
template <class T, int Lanes>
[[nodiscard]] T sum_run_by_tree_(T const* first, std::size_t count) noexcept {
    constexpr auto lanes = static_cast<std::size_t>(Lanes);
    TreeAdd add{};
    if (count < lanes) return tree_of_run_<TreeLeaves::Inline>(first, count, add);
    if (has_single_bit(count)) return reduce_tree_lanes_<T, Lanes>(tree_block_vector_<T, Lanes>(first, count / lanes));
    std::size_t const split = bit_floor(count - 1);
    T const left = sum_run_by_tree_<T, Lanes>(first, split);
    T const right = sum_run_by_tree_<T, Lanes>(first + split, count - split);
    return left + right;
}

}  // namespace CRUCIBLE_SIMD_TARGET

// The count of the lanes of a test of sum_by_tree_in_lanes: a power of two from 2
// to 64.
template <int Lanes>
concept TreeLanes = Lanes >= 2 && Lanes <= 64 && has_single_bit(static_cast<unsigned>(Lanes));

}  // namespace detail

// The tree of the elements, by the rule of the order, with the combine of
// the caller, or no value for an empty View.  The combine runs one time for
// each element after the first.  Complexity: O(n) combines.
template <class T, std::size_t Extent, class Combine>
    requires TreeValue<std::remove_const_t<T>> && TreeCombine<Combine, std::remove_const_t<T>>
[[nodiscard]] constexpr Option<std::remove_const_t<T>> reduce_by_tree(View<T, Extent> elements,
                                                                      Combine combine) noexcept {
    using Value = std::remove_const_t<T>;
    if (elements.size() == 0) return none;
    Value const* const first = first_of_(detail::BaseLayerRead{}, elements);
    return Option<Value>::some(detail::tree_of_run_<detail::TreeLeaves::Blocked>(first, elements.size(), combine));
}

// A combine that holds a linear value by value binds to this overload, and to
// the same twin of each other door of a tree and of a scan, so the refusal
// gives its reason (LinearTreeCombine).
template <class T, std::size_t Extent, class Combine>
    requires LinearTreeCombine<Combine, std::remove_const_t<T>>
void reduce_by_tree(View<T, Extent> elements, Combine combine) = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

inline namespace CRUCIBLE_SIMD_TARGET {

// The tree sum of the elements with Lanes lanes in each vector.  The count
// of lanes changes no bit (the lanes), so a test compares each count.  An
// empty View gives +0.  Complexity: O(n).
template <int Lanes, class T>
    requires TreeSumElement<T> && detail::TreeLanes<Lanes>
[[nodiscard]] T sum_by_tree_in_lanes(View<T const> elements) noexcept {
    if (elements.size() == 0) return T{};
    return fp::canonicalize_nan(
        detail::sum_run_by_tree_<T, Lanes>(first_of_(detail::BaseLayerRead{}, elements), elements.size()));
}

// The tree sum of the elements, with the lanes of one register of the
// target.  An empty View gives +0.  Complexity: O(n).
[[nodiscard]] inline float sum_by_tree(View<float const> elements) noexcept {
    return sum_by_tree_in_lanes<simd::native_lanes<float>>(elements);
}

[[nodiscard]] inline double sum_by_tree(View<double const> elements) noexcept {
    return sum_by_tree_in_lanes<simd::native_lanes<double>>(elements);
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
    if (count == 1) return simd::load<V>(first_of_(BaseLayerRead{}, runs[0]) + offset);
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
    if (count == 1) return first_of_(BaseLayerRead{}, runs[0])[offset];
    std::size_t const split = bit_floor(count - 1);
    T const left = tree_runs_element_<T>(runs, split, offset);
    T const right = tree_runs_element_<T>(runs + split, count - split, offset);
    return left + right;
}

// True when a run and the sums of count elements each share an element and
// do not start at one address.  The test compares the two address ranges as
// integers, as offset_of of Region.h does, so two runs of different objects
// make no undefined compare of pointers.  Complexity: O(1).
template <class T>
[[nodiscard]] bool is_partial_overlap_(T const* run, T const* sums, std::size_t count) noexcept {
    std::uintptr_t const run_start = __builtin_bit_cast(std::uintptr_t, run);
    std::uintptr_t const sums_start = __builtin_bit_cast(std::uintptr_t, sums);
    // The door of a View bounds its bytes by __PTRDIFF_MAX__, and an object
    // ends below the top of the address space, so no sum overflows.
    std::uintptr_t const bytes = count * sizeof(T);
    if (run_start == sums_start) return false;
    return run_start < sums_start + bytes && sums_start < run_start + bytes;
}

}  // namespace detail

// The error of a sum of runs.
enum class RunsError : std::uint8_t {
    // A run does not hold the count of the sums.
    LengthMismatch = 0,
    // A run and the sums share some elements, and they do not start at one
    // element.
    PartialOverlap = 1,
};

inline namespace CRUCIBLE_SIMD_TARGET {

// The tree sum of the runs element by element: sums[i] is the tree of the
// elements at i of the runs, in run order, by the rule of the order.  So a
// sum of K producers into one result has a fixed topology, and the order in
// which the producers finish changes no bit.  No run gives +0 in each sum.
//
// Each run must hold the count of sums, else the call gives LengthMismatch.
// The sums can be the same View as one run or more: each vector of sums is
// written after the reads of each run at its positions.  A run that shares
// some elements with the sums, and does not start at the first element of the
// sums, gives PartialOverlap.  Each error comes before the first write.
// Complexity: O(K) checks, then O(K * n) loads and adds, with a recursion of
// depth ceil(log2 K) for each vector of sums.
template <class T, class Brand, std::size_t Extent>
    requires TreeSumElement<T>
[[nodiscard]] Result<Unit, RunsError> sum_runs_by_tree(View<View<T const> const> runs,
                                                       ViewMut<T, Brand, Extent> sums) noexcept {
    constexpr int lanes = simd::native_lanes<T>;
    std::size_t const count = sums.size();
    T* const out = first_of_(detail::BaseLayerRead{}, sums);
    for (View<T const> const run : runs) {
        if (run.size() != count) return err(RunsError::LengthMismatch);
        if (detail::is_partial_overlap_(first_of_(detail::BaseLayerRead{}, run), out, count)) {
            return err(RunsError::PartialOverlap);
        }
    }
    if (runs.size() == 0) {
        fill(std::move(sums), T{});
        return Unit{};
    }
    View<T const> const* const first_run = first_of_(detail::BaseLayerRead{}, runs);
    std::size_t const whole = count - count % static_cast<std::size_t>(lanes);
    for (std::size_t offset = 0; offset < whole; offset += static_cast<std::size_t>(lanes)) {
        simd::store(simd::canonicalize_nans(detail::tree_runs_vector_<T, lanes>(first_run, runs.size(), offset)),
                    out + offset);
    }
    for (std::size_t offset = whole; offset < count; ++offset) {
        out[offset] = fp::canonicalize_nan(detail::tree_runs_element_<T>(first_run, runs.size(), offset));
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
    std::size_t const* const offset = first_of_(detail::BaseLayerRead{}, offsets);
    for (std::size_t segment = 0; segment < results; ++segment) {
        if (offset[segment + 1] < offset[segment]) return err(SegmentError::OffsetsOutOfOrder);
    }
    if (offset[results] > elements) return err(SegmentError::OffsetPastEnd);
    return Unit{};
}

namespace detail {

inline namespace CRUCIBLE_SIMD_TARGET {

// The tree sums of the segments into the count sums from out.  The kernel of
// sum_segments_by_tree on a raw run, which a parallel form calls for the run of
// a shard of its region.  The raw run stays until a region has a frame door
// that gives a ViewMut.
template <class T>
[[nodiscard]] Result<Unit, SegmentError> sum_segments_by_tree_(View<T const> elements, View<std::size_t const> offsets,
                                                               T* out, std::size_t count) noexcept {
    auto const checked = check_segment_offsets(elements.size(), offsets, count);
    if (checked.is_err()) return checked;
    T const* const first = first_of_(BaseLayerRead{}, elements);
    std::size_t const* const offset = first_of_(BaseLayerRead{}, offsets);
    for (std::size_t segment = 0; segment < count; ++segment) {
        std::size_t const size = offset[segment + 1] - offset[segment];
        out[segment] =
            size == 0 ? T{}
                      : fp::canonicalize_nan(sum_run_by_tree_<T, simd::native_lanes<T>>(first + offset[segment], size));
    }
    return Unit{};
}

}  // namespace CRUCIBLE_SIMD_TARGET

}  // namespace detail

inline namespace CRUCIBLE_SIMD_TARGET {

// The tree sum of each segment: sums[s] is the tree sum of the elements
// [offsets[s], offsets[s + 1]), and an empty segment gives +0.  The offsets
// hold one more element than the sums, they do not decrease, and the last one
// is at most the count of the elements.  Else the call gives a SegmentError
// and writes nothing.  Complexity: O(n + S).
template <class T, class Brand, std::size_t Extent>
    requires TreeSumElement<T>
[[nodiscard]] Result<Unit, SegmentError> sum_segments_by_tree(View<T const> elements, View<std::size_t const> offsets,
                                                              ViewMut<T, Brand, Extent> sums) noexcept {
    return detail::sum_segments_by_tree_(elements, offsets, first_of_(detail::BaseLayerRead{}, sums), sums.size());
}

}  // namespace CRUCIBLE_SIMD_TARGET

// The tree of each segment with the combine of the caller: results[s] is the
// tree of the elements [offsets[s], offsets[s + 1]), and an empty segment
// gives empty.  The offsets obey the rules of sum_segments_by_tree.
// Complexity: O(n + S) combines.
template <class T, class Brand, std::size_t Extent, class Combine>
    requires TreeValue<T> && TreeCombine<Combine, T> && std::is_nothrow_copy_assignable_v<T>
[[nodiscard]] constexpr Result<Unit, SegmentError>
reduce_segments_by_tree(View<T const> elements, View<std::size_t const> offsets, ViewMut<T, Brand, Extent> results,
                        T const& empty, Combine combine) noexcept {
    auto const checked = check_segment_offsets(elements.size(), offsets, results.size());
    if (checked.is_err()) return checked;
    T const* const first = first_of_(detail::BaseLayerRead{}, elements);
    std::size_t const* const offset = first_of_(detail::BaseLayerRead{}, offsets);
    T* const out = first_of_(detail::BaseLayerRead{}, results);
    for (std::size_t segment = 0; segment < results.size(); ++segment) {
        std::size_t const count = offset[segment + 1] - offset[segment];
        out[segment] = count == 0
                         ? empty
                         : detail::tree_of_run_<detail::TreeLeaves::Blocked>(first + offset[segment], count, combine);
    }
    return Unit{};
}

template <class T, class Brand, std::size_t Extent, class Combine>
    requires LinearTreeCombine<Combine, T>
void reduce_segments_by_tree(View<T const> elements, View<std::size_t const> offsets, ViewMut<T, Brand, Extent> results,
                             T const& empty, Combine combine) = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

// ── The split into grains ───────────────────────────────────────────────

// The count of the grains of a run of count elements: each grain holds Grain
// elements, and the last grain holds the rest.
template <std::size_t Grain>
    requires TreeGrain<Grain>
[[nodiscard]] constexpr std::size_t count_tree_grains(std::size_t count) noexcept {
    return count / Grain + (count % Grain != 0 ? 1 : 0);
}

namespace detail {

// The end of a range of grains: first_grain + partials, which must be at most
// the count of the grains of the run.
template <std::size_t Grain>
constexpr void check_grain_range_(std::size_t count, std::size_t first_grain, std::size_t partials) noexcept {
    std::size_t const grains = count_tree_grains<Grain>(count);
    if (first_grain > grains || partials > grains - first_grain) [[unlikely]] {
        fatal("a range of {} grains from grain {} is past the {} grains of the run", partials, first_grain, grains);
    }
}

// The tree of each grain of the range with the combine of the caller into the
// partials partials from out.  The kernel of reduce_grains_by_tree on a raw run,
// which a parallel form calls for the run of a shard of its region.  The raw
// run stays until a region has a frame door that gives a ViewMut.
template <std::size_t Grain, class T, class Combine>
constexpr void reduce_grains_by_tree_(View<T const> elements, std::size_t first_grain, T* out, std::size_t partials,
                                      Combine& combine) noexcept {
    std::size_t const count = elements.size();
    check_grain_range_<Grain>(count, first_grain, partials);
    T const* const first = first_of_(BaseLayerRead{}, elements);
    for (std::size_t index = 0; index < partials; ++index) {
        std::size_t const start = (first_grain + index) * Grain;
        std::size_t const size = count - start < Grain ? count - start : Grain;
        out[index] = tree_of_run_<TreeLeaves::Blocked>(first + start, size, combine);
    }
}

inline namespace CRUCIBLE_SIMD_TARGET {

// The tree sum of each grain of the range into the partials partials from
// out: the kernel of sum_grains_by_tree on a raw run, as reduce_grains_by_tree_ is.
template <std::size_t Grain, class T>
void sum_grains_by_tree_(View<T const> elements, std::size_t first_grain, T* out, std::size_t partials) noexcept {
    std::size_t const count = elements.size();
    check_grain_range_<Grain>(count, first_grain, partials);
    T const* const first = first_of_(BaseLayerRead{}, elements);
    for (std::size_t index = 0; index < partials; ++index) {
        std::size_t const start = (first_grain + index) * Grain;
        std::size_t const size = count - start < Grain ? count - start : Grain;
        out[index] = fp::canonicalize_nan(sum_run_by_tree_<T, simd::native_lanes<T>>(first + start, size));
    }
}

}  // namespace CRUCIBLE_SIMD_TARGET

}  // namespace detail

inline namespace CRUCIBLE_SIMD_TARGET {

// The tree sum of each grain of the range: partials[i] is the tree sum of the
// grain first_grain + i of the elements.  The tree sum of all the partials of
// the run is then the tree sum of the run, so sum_by_tree(partials) gives it.  A
// range past the grains of the run ends the process.  Complexity: O(Grain)
// for each partial.
template <std::size_t Grain, class T, class Brand, std::size_t Extent>
    requires TreeGrain<Grain> && TreeSumElement<T>
void sum_grains_by_tree(View<T const> elements, std::size_t first_grain, ViewMut<T, Brand, Extent> partials) noexcept {
    detail::sum_grains_by_tree_<Grain>(elements, first_grain, first_of_(detail::BaseLayerRead{}, partials),
                                       partials.size());
}

}  // namespace CRUCIBLE_SIMD_TARGET

// The tree of each grain of the range with the combine of the caller:
// partials[i] is the tree of the grain first_grain + i.  reduce_by_tree of all
// the partials of the run then gives the tree of the run.  A range past the
// grains of the run ends the process.
template <std::size_t Grain, class T, class Brand, std::size_t Extent, class Combine>
    requires TreeGrain<Grain> && TreeValue<T> && TreeCombine<Combine, T> && std::is_nothrow_copy_assignable_v<T>
constexpr void reduce_grains_by_tree(View<T const> elements, std::size_t first_grain,
                                     ViewMut<T, Brand, Extent> partials, Combine combine) noexcept {
    detail::reduce_grains_by_tree_<Grain>(elements, first_grain, first_of_(detail::BaseLayerRead{}, partials),
                                          partials.size(), combine);
}

template <std::size_t Grain, class T, class Brand, std::size_t Extent, class Combine>
    requires LinearTreeCombine<Combine, T>
void reduce_grains_by_tree(View<T const> elements, std::size_t first_grain, ViewMut<T, Brand, Extent> partials,
                           Combine combine) = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

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

// Writes the tree of steps.size() + 1 leaves, by the rule of the order, as
// steps.size() combine steps.  Each step comes after the steps of its
// operands, and the last step gives the root.  A net check reads the plan as
// the specification of the order.  A plan of more than max_plan_leaves
// leaves gives TooManyLeaves and writes nothing.  Complexity: O(n).
template <class Brand, std::size_t Extent>
[[nodiscard]] constexpr Result<Unit, PlanError> write_tree_plan(ViewMut<TreeStep, Brand, Extent> steps) noexcept {
    if (steps.size() >= max_plan_leaves) return err(PlanError::TooManyLeaves);
    auto const leaves = static_cast<std::uint32_t>(steps.size() + 1);
    std::uint32_t next = 0;
    static_cast<void>(detail::write_tree_plan_(first_of_(detail::BaseLayerRead{}, steps), leaves, 0, leaves, next));
    return Unit{};
}

// The plan of the tree (the definition below).  ReduceFrameKeyDoor names it as
// a friend, so this declaration stands before the door.
template <std::size_t Leaves>
    requires(Leaves >= 2 && Leaves <= 4096)
[[nodiscard]] consteval FixedArray<TreeStep, Leaves - 1> plan_tree() noexcept;

// The door of the plan of the tree to the key of the frame doors (the doors
// of the owners above foundation/core/Frame.h).  Its one member is private,
// and plan_tree is its one friend.  The invariant: the plan reads the run of
// the FixedArray that it builds in its own frame and gives by value.  A home
// owner row of utils/rule-table.txt keeps this definition in this header.
class ReduceFrameKeyDoor final : ::foundation::NoObject<ReduceFrameKeyDoor> {
    template <std::size_t Leaves>
        requires(Leaves >= 2 && Leaves <= 4096)
    friend consteval auto ::foundation::core::plan_tree() noexcept -> FixedArray<TreeStep, Leaves - 1>;

    // The key of the frame doors, for the run of the array of the plan.
    [[nodiscard]] CRUCIBLE_INLINE static constexpr detail::FrameKey key_() noexcept { return detail::FrameDoor::key(); }
};

// The plan of the tree of Leaves leaves, as a constant.  The plan writes its
// own array, so it calls the writer of write_tree_plan with no frame door.
template <std::size_t Leaves>
    requires(Leaves >= 2 && Leaves <= 4096)
[[nodiscard]] consteval FixedArray<TreeStep, Leaves - 1> plan_tree() noexcept {
    FixedArray<TreeStep, Leaves - 1> steps{};
    std::uint32_t next = 0;
    static_cast<void>(detail::write_tree_plan_(
        first_of_(detail::BaseLayerRead{}, frame_run_(ReduceFrameKeyDoor::key_(), steps)), Leaves, 0, Leaves, next));
    return steps;
}

// The value of a plan over the leaves, with the combine of the caller.  The
// scratch holds the value of each step.  The leaves hold one more element than
// the steps, and the scratch holds as many as the steps.  A step that reads a
// node of a later step gives NodeNotReady.  Complexity: O(steps) combines.
template <class T, class Brand, std::size_t Extent, class Combine>
    requires TreeValue<T> && TreeCombine<Combine, T> && std::is_nothrow_copy_assignable_v<T>
[[nodiscard]] constexpr Result<T, PlanError> evaluate_tree_plan(View<TreeStep const> steps, View<T const> leaves,
                                                                ViewMut<T, Brand, Extent> scratch,
                                                                Combine combine) noexcept {
    std::size_t const count = steps.size();
    if (leaves.size() != count + 1 || scratch.size() != count) return err(PlanError::CountMismatch);
    TreeStep const* const step = first_of_(detail::BaseLayerRead{}, steps);
    T const* const leaf = first_of_(detail::BaseLayerRead{}, leaves);
    T* const value = first_of_(detail::BaseLayerRead{}, scratch);
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

template <class T, class Brand, std::size_t Extent, class Combine>
    requires LinearTreeCombine<Combine, T>
void evaluate_tree_plan(View<TreeStep const> steps, View<T const> leaves, ViewMut<T, Brand, Extent> scratch,
                        Combine combine) = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

}  // namespace foundation::core
