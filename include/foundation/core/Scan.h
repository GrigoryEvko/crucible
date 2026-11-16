#pragma once

// The prefix scans of the Region family, in one stated order.  The result
// has the same bits for each thread count, each schedule and each host.
//
//   inclusive_scan(in, out, combine)         out[k] combines in[0] to in[k]
//   exclusive_scan(in, out, empty, combine)  out[k] combines in[0] to
//                                            in[k - 1], and out[0] is empty
//   inclusive_sum_scan(in, out), exclusive_sum_scan(in, out)
//                                            the scans of a float or a double
//                                            with an add, in SIMD lanes, and
//                                            the forms _lanes<L> for a test of
//                                            the rule that the lanes change no
//                                            bit
//   inclusive_scan_grains<G>(...), exclusive_scan_grains<G>(...),
//   inclusive_sum_scan_grains<G>(...), exclusive_sum_scan_grains<G>(...)
//                                            the scan of a range of grains of
//                                            G elements with the carry of each
//                                            grain, for a parallel scan
//   exclusive_scan_depth(k), inclusive_scan_depth(k)
//                                            the depth of out[k], which gives
//                                            the error bound
//   scan_plan_steps(n, kind), write_scan_plan(...), evaluate_scan_plan(...)
//                                            the scan as a list of combine
//                                            steps, and an evaluation of such a
//                                            list
//
// Each output is a ViewMut of a frame door (foundation/core/Region.h), which
// the call takes by value, so the output has one writer by its type.  The
// inputs are Views.
//
// THE ORDER
//   Write k as a sum of different powers of two, from the largest.  The
//   prefix [0, k) divides into blocks of these sizes, in that order, and each
//   block is aligned at a multiple of its size.  The exclusive prefix F(k) is
//   the left fold of the tree sums of the blocks, from the largest block:
//   F(k) = ((B1 + B2) + B3) + ... + Br.  The tree sum of a block is the tree
//   of foundation/core/Reduce.h, a full balanced tree.  F(0) has no element.
//
//     exclusive_scan: out[0] = empty, out[k] = F(k)
//     inclusive_scan: out[0] = in[0], out[k] = F(k) + in[k]
//
//   F(k) depends only on in[0] to in[k - 1], so a scan of a longer run gives
//   the same bits at each position of the shorter run.  The last element of
//   an inclusive scan is in general not the bits of tree_sum of the run: the
//   blocks of the scan fold from the left, and the blocks of the tree combine
//   from the right.
//
//   The implementation is the recursion of the Blelloch scan on the tree of
//   Reduce.h.  Each node takes the carry F(a) of its first position a.  Its
//   left part takes the same carry, and its right part takes the carry
//   F(a) + S, where S is the tree sum of the left part.  The left part of
//   each node is an aligned block of a power of two, so the carries are the
//   folds of THE ORDER.  The work is O(n), with at most two combines for each
//   element.
//
// THE GRAINS
//   A grain of G elements, a power of two, starts at a multiple of G.  The
//   carry of grain g is F(g * G).  The blocks of g * G hold whole grains, so
//   the carries of the grains are the exclusive scan of the tree sums of the
//   grains.  A parallel scan then takes three phases: the tree sum of each
//   grain (tree_sum_grains of Reduce.h), the exclusive scan of these sums,
//   and the scan of each grain with its carry.  A thread count, a schedule
//   and a grain change no operand of any combine.
//
// THE ERROR BOUND
//   Each element of a block passes through the tree of the block, and then
//   through the folds that come after the block.  So the depth of out[k] is
//   floor(log2 k) + popcount(k) - 1 for an exclusive scan with k >= 1, and
//   floor(log2 k) + popcount(k) for an inclusive scan with k >= 1.  Each depth
//   is at most 2 * floor(log2 k) + 1.  For a sum with the round to nearest of
//   IEEE 754, the error of out[k] is at most gamma(d) * sum |x_i| over the
//   elements of the prefix, with d the depth, gamma(d) = d * u / (1 - d * u),
//   and u = 2^-24 for float and 2^-53 for double.
//
// THE VALUES OUTSIDE THE BOUND
//   The rule of THE VALUES OUTSIDE THE BOUND of Reduce.h holds for each sum
//   scan: each output that is a NaN is the canonical NaN quiet_nan_of<T>,
//   with one compare and one select at each output.  So the inclusive output
//   of a first element that is a NaN is the canonical NaN too, and not a copy
//   of that element.  The scans with the combine of a caller give the bits of
//   the combine.  The exclusive out[0] of a sum scan is +0.
//
// THE LANES
//   A sum scan loads L consecutive elements into one vector, with L the count
//   of lanes of one register of the target.  The block of the vector is
//   aligned, so its prefixes are its carry F(start) and then the blocks of the
//   position inside it, from the largest.  The up-sweep adds each lane and the
//   lane of its sibling block, level by level, so each lane holds the tree sum
//   of the aligned block of that level that holds it.  In the upper lane of a
//   pair the add takes its two operands in the other order.  An add of IEEE
//   754 is commutative for each value that is not a NaN, so each such sum has
//   the bits of the tree.  A NaN output takes the canonical NaN, so the order
//   of the operands of a NaN changes no bit either.  The down-sweep adds the carry and then one
//   term of each level, from the largest block.  A lane with no block at a
//   level adds -0, which is exact for each value.  A binary counter of the
//   sums of the blocks gives the carry of each vector.  So 4, 8 or 16 lanes
//   compute the folds of THE ORDER, and a test compares each count of lanes
//   with the recursion.
//
// THE COSTS
//   A scan makes at most two combines for each element, with a recursion of
//   depth ceil(log2 n) + popcount(n).  A sum scan makes about 2 * log2(L)
//   shuffles and adds for each vector of L lanes.  No scan allocates.  in can
//   be the View of the run of out, and no other overlap is permitted.
//
// THE TARGET
//   The sum scans and their grain forms read the vector width of the target.
//   So they and their SIMD helpers are in the inline namespace
//   CRUCIBLE_SIMD_TARGET (THE TARGET NAMESPACE of foundation/Platform.h).  The
//   scans with the combine of a caller and the plans stay outside it.

#include <foundation/Platform.h>
#include <foundation/Simd.h>
#include <foundation/core/Bits.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Reduce.h>
#include <foundation/core/Region.h>
#include <foundation/core/Report.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace foundation::core {

// The kind of a scan.
enum class ScanKind : std::uint8_t {
    // out[k] combines the elements to k.
    Inclusive = 0,
    // out[k] combines the elements before k.
    Exclusive = 1,
};

// The depth of out[position] of an exclusive scan, and of an inclusive scan.
// The error bound of THE ERROR BOUND reads it.
[[nodiscard]] constexpr int exclusive_scan_depth(std::size_t position) noexcept {
    return position == 0 ? 0 : bit_width(position) - 1 + popcount(position) - 1;
}

[[nodiscard]] constexpr int inclusive_scan_depth(std::size_t position) noexcept {
    return position == 0 ? 0 : bit_width(position) - 1 + popcount(position);
}

namespace detail {

// The values of one scan: the elements, the output, the combine and the
// empty value of an exclusive scan.
template <class T, class Combine, ScanKind Kind>
struct ScanRun {
    T const* in = nullptr;
    T* out = nullptr;
    Combine* combine = nullptr;
    T const* empty = nullptr;
};

// The scan of the count elements from position, a node of the tree, with
// the carry F(position).  carry is null when the node has no element before
// it.  The result is the tree sum of the node when is_sum_needed is true, and
// any value otherwise.  The read of in[a] comes before the write of out[a],
// so out can be the same View as in.  A node of two elements is a base case,
// so the carry into its second element is the inclusive value of its first.
template <class T, class Combine, ScanKind Kind>
[[nodiscard]] constexpr T scan_node_(ScanRun<T, Combine, Kind> const& run, std::size_t position, std::size_t count,
                                     T const* carry, bool is_sum_needed) noexcept {
    Combine& combine = *run.combine;
    if (count == 1) {
        T const value = run.in[position];
        if constexpr (Kind == ScanKind::Inclusive) {
            run.out[position] = carry == nullptr ? value : combine(*carry, value);
        } else {
            run.out[position] = carry == nullptr ? *run.empty : *carry;
        }
        return value;
    }
    if (count == 2) {
        T const first = run.in[position];
        T const second = run.in[position + 1];
        T const between = carry == nullptr ? first : combine(*carry, first);
        if constexpr (Kind == ScanKind::Inclusive) {
            T const last = combine(between, second);
            run.out[position] = between;
            run.out[position + 1] = last;
            // With no carry, the last inclusive value is the sum of the pair.
            if (carry == nullptr) return last;
        } else {
            run.out[position] = carry == nullptr ? *run.empty : *carry;
            run.out[position + 1] = between;
        }
        return is_sum_needed ? combine(first, second) : first;
    }
    std::size_t const split = bit_floor(count - 1);
    T const left = scan_node_(run, position, split, carry, true);
    T const right_carry = carry == nullptr ? left : combine(*carry, left);
    T const right = scan_node_(run, position + split, count - split, &right_carry, is_sum_needed);
    return is_sum_needed ? combine(left, right) : left;
}

// The scan of a run with the carry of its first element, or none.
template <ScanKind Kind, class T, class Combine>
constexpr void scan_run_(T const* in, T* out, std::size_t count, T const* carry, T const* empty,
                         Combine& combine) noexcept {
    if (count == 0) return;
    ScanRun<T, Combine, Kind> const run{in, out, &combine, empty};
    static_cast<void>(scan_node_(run, 0, count, carry, false));
}

}  // namespace detail

// The inclusive scan of in into out, by THE ORDER, with the combine of the
// caller.  The two runs hold one count, and two counts that differ give
// LengthMismatch with no write.  in can be the View of the run of out.
// Complexity: at most 2n combines.
template <class T, class Brand, std::size_t Extent, class Combine>
    requires TreeValue<T> && TreeCombine<Combine, T> && std::is_nothrow_copy_assignable_v<T>
[[nodiscard]] constexpr Result<Unit, LengthMismatch> inclusive_scan(View<T const> in, ViewMut<T, Brand, Extent> out,
                                                                    Combine combine) noexcept {
    if (in.size() != out.size()) return err(LengthMismatch{});
    detail::scan_run_<ScanKind::Inclusive>(first_of_(detail::ViewDoor::key(), in),
                                           first_of_(detail::ViewDoor::key(), out), in.size(),
                                           static_cast<T const*>(nullptr), static_cast<T const*>(nullptr), combine);
    return Unit{};
}

// The exclusive scan of in into out, by THE ORDER: out[0] is empty, and
// out[k] combines in[0] to in[k - 1].  The two runs hold one count, and two
// counts that differ give LengthMismatch with no write.  Complexity: at most
// 2n combines.
template <class T, class Brand, std::size_t Extent, class Combine>
    requires TreeValue<T> && TreeCombine<Combine, T> && std::is_nothrow_copy_assignable_v<T>
[[nodiscard]] constexpr Result<Unit, LengthMismatch> exclusive_scan(View<T const> in, ViewMut<T, Brand, Extent> out,
                                                                    T const& empty, Combine combine) noexcept {
    if (in.size() != out.size()) return err(LengthMismatch{});
    detail::scan_run_<ScanKind::Exclusive>(first_of_(detail::ViewDoor::key(), in),
                                           first_of_(detail::ViewDoor::key(), out), in.size(),
                                           static_cast<T const*>(nullptr), &empty, combine);
    return Unit{};
}

// ── The sum scans with SIMD lanes ───────────────────────────────────────

namespace detail {

inline namespace CRUCIBLE_SIMD_TARGET {

// The sums of the sibling blocks of level Level: lane j takes lane
// j ^ 2^Level.  When the sums hold in lane j the sum of the aligned block of
// 2^Level elements that holds j, this is the sum of the other half of the
// block of the next level.
template <class T, int Lanes, int Level, int... Lane>
[[nodiscard]] CRUCIBLE_INLINE simd::vec<T, Lanes> swap_scan_level_(simd::vec<T, Lanes> sums,
                                                                   simd::detail::LaneList<Lane...> /*lanes*/) noexcept {
    return simd::vec<T, Lanes>{__builtin_shufflevector(sums.raw(), sums.raw(), (Lane ^ (1 << Level))...)};
}

// The terms of level Level: lane j takes the sum of the sibling block when
// bit Level of j is set, which is the block just before the block of j, and
// -0 otherwise.  An add of -0 is exact for each value, so a lane with no term
// at a level keeps its bits.  The pattern takes each lane from its own
// position, so the shuffle is a blend.
template <class T, int Lanes, int Level, int... Lane>
[[nodiscard]] CRUCIBLE_INLINE simd::vec<T, Lanes>
scan_level_terms_(simd::vec<T, Lanes> siblings, simd::detail::LaneList<Lane...> /*lanes*/) noexcept {
    simd::vec<T, Lanes> const minus_zero{-T{}};
    return simd::vec<T, Lanes>{__builtin_shufflevector(siblings.raw(), minus_zero.raw(),
                                                       (((Lane >> Level) & 1) != 0 ? Lane : Lanes + Lane)...)};
}

// The exclusive prefixes of the Lanes elements of one aligned block, with the
// carry F(start) of the block in each lane, and the tree sum of the block in
// total.  sums holds in lane j the sum of the aligned block of 2^Level
// elements that holds j.  The recursion forms the sums of each level on its
// way down, the up-sweep.  On its way up it adds the carry and then the term
// of each level, from the largest block, the left fold of THE ORDER.  Each
// vector has the full width, so no step stores a part of a register: a
// shuffle of a narrow vector into a wide one gave a failed store forwarding
// on each block, and the scan ran at 2.3 ns for each element.
template <class T, int Lanes, int Level>
[[nodiscard]] CRUCIBLE_INLINE simd::vec<T, Lanes> scan_block_prefix_(simd::vec<T, Lanes> sums,
                                                                     simd::vec<T, Lanes> carry, T& total) noexcept {
    if constexpr ((1 << Level) == Lanes) {
        total = sums[0];
        return carry;
    } else {
        simd::vec<T, Lanes> const siblings =
            swap_scan_level_<T, Lanes, Level>(sums, simd::detail::lane_list_t<Lanes>{});
        simd::vec<T, Lanes> const prefix = scan_block_prefix_<T, Lanes, Level + 1>(sums + siblings, carry, total);
        return prefix + scan_level_terms_<T, Lanes, Level>(siblings, simd::detail::lane_list_t<Lanes>{});
    }
}

// The sum scan of the count elements from in, with vectors of Lanes lanes,
// and the carry of the first element, or none.  Each aligned block of Lanes
// elements takes its carry from a binary counter of the tree sums of the
// blocks before it: an entry holds the sum of a full aligned block and the
// fold of the entries from the bottom to it, so the fold of the top entry is
// F(start) of the next block.  The elements after the last whole block take
// the scalar recursion with that carry.  The bits are the bits of scan_run_
// with an add: a lane with no carry adds -0, which is exact, and the
// exclusive out[0] of a run with no carry is +0.
template <ScanKind Kind, class T, int Lanes>
void sum_scan_lanes_(T const* in, T* out, std::size_t count, T const* carry) noexcept {
    using V = simd::vec<T, Lanes>;
    constexpr auto lanes = static_cast<std::size_t>(Lanes);
    T const base = carry == nullptr ? -T{} : *carry;
    // At most popcount(count / Lanes) entries, so 64 entries hold each count.
    T sums[64]{};
    T folds[64]{};
    std::size_t top = 0;
    std::size_t const blocks = count / lanes;
    for (std::size_t block = 0; block < blocks; ++block) {
        V const values = simd::load<V>(in + block * lanes);
        T total{};
        V const prefix = scan_block_prefix_<T, Lanes, 0>(values, V{top == 0 ? base : folds[top - 1]}, total);
        if constexpr (Kind == ScanKind::Inclusive) {
            simd::store(canonical_sums_<T, Lanes>(prefix + values), out + block * lanes);
        } else {
            simd::store(canonical_sums_<T, Lanes>(prefix), out + block * lanes);
        }
        T sum = total;
        for (int merges = countr_one(block); merges > 0; --merges) {
            sum = sums[top - 1] + sum;
            --top;
        }
        folds[top] = (top == 0 ? base : folds[top - 1]) + sum;
        sums[top] = sum;
        ++top;
    }
    std::size_t const done = blocks * lanes;
    T const empty{};
    if (done < count) {
        T const tail_carry = top == 0 ? base : folds[top - 1];
        bool const has_carry = top != 0 || carry != nullptr;
        TreeAdd add{};
        scan_run_<Kind>(in + done, out + done, count - done, has_carry ? &tail_carry : nullptr, &empty, add);
        // The tail holds fewer than Lanes outputs, and an inclusive output of
        // a run with no carry is a copy of its element.
        for (std::size_t position = done; position < count; ++position) {
            out[position] = canonical_sum_(out[position]);
        }
    }
    if constexpr (Kind == ScanKind::Exclusive) {
        if (carry == nullptr && count > 0) out[0] = empty;
    }
}

}  // namespace CRUCIBLE_SIMD_TARGET

}  // namespace detail

inline namespace CRUCIBLE_SIMD_TARGET {

// The sum scans with Lanes lanes in each vector.  The count of lanes changes
// no bit, so a test compares each count.  The empty value of the exclusive
// sum is +0.  Complexity: O(n), with about log2(Lanes) shuffles and adds for
// each vector in each direction.
template <int Lanes, class T, class Brand, std::size_t Extent>
    requires TreeSumElement<T> && detail::TreeLanes<Lanes>
[[nodiscard]] Result<Unit, LengthMismatch> inclusive_sum_scan_lanes(View<T const> in,
                                                                    ViewMut<T, Brand, Extent> out) noexcept {
    if (in.size() != out.size()) return err(LengthMismatch{});
    detail::sum_scan_lanes_<ScanKind::Inclusive, T, Lanes>(first_of_(detail::ViewDoor::key(), in),
                                                           first_of_(detail::ViewDoor::key(), out), in.size(),
                                                           static_cast<T const*>(nullptr));
    return Unit{};
}

template <int Lanes, class T, class Brand, std::size_t Extent>
    requires TreeSumElement<T> && detail::TreeLanes<Lanes>
[[nodiscard]] Result<Unit, LengthMismatch> exclusive_sum_scan_lanes(View<T const> in,
                                                                    ViewMut<T, Brand, Extent> out) noexcept {
    if (in.size() != out.size()) return err(LengthMismatch{});
    detail::sum_scan_lanes_<ScanKind::Exclusive, T, Lanes>(first_of_(detail::ViewDoor::key(), in),
                                                           first_of_(detail::ViewDoor::key(), out), in.size(),
                                                           static_cast<T const*>(nullptr));
    return Unit{};
}

// The sum scans: the scans with the add of a tree sum, with the lanes of one
// register of the target.  The bits are the bits of inclusive_scan and
// exclusive_scan with an add.  The empty value of the exclusive sum is +0.
template <class T, class Brand, std::size_t Extent>
    requires TreeSumElement<T>
[[nodiscard]] Result<Unit, LengthMismatch> inclusive_sum_scan(View<T const> in,
                                                              ViewMut<T, Brand, Extent> out) noexcept {
    return inclusive_sum_scan_lanes<simd::native_lanes<T>>(in, std::move(out));
}

template <class T, class Brand, std::size_t Extent>
    requires TreeSumElement<T>
[[nodiscard]] Result<Unit, LengthMismatch> exclusive_sum_scan(View<T const> in,
                                                              ViewMut<T, Brand, Extent> out) noexcept {
    return exclusive_sum_scan_lanes<simd::native_lanes<T>>(in, std::move(out));
}

}  // namespace CRUCIBLE_SIMD_TARGET

// ── The split into grains ───────────────────────────────────────────────

namespace detail {

// Calls scan_one for each grain of the range, with its input, its output,
// its count and its carry, or none for grain 0.  The outputs from target
// hold the outputs of the elements of the grains of the range, from the
// first element of grain first_grain.  A count of outputs that differs from
// the count of those elements, or a range past the grains of the run, ends
// the process.
template <std::size_t Grain, class T, class ScanOne>
constexpr void for_each_scan_grain_(View<T const> in, std::size_t first_grain, View<T const> carries, T* target,
                                    std::size_t outputs, ScanOne scan_one) noexcept {
    std::size_t const count = in.size();
    check_grain_range_<Grain>(count, first_grain, carries.size());
    std::size_t const begin = first_grain * Grain;
    std::size_t const limit = (first_grain + carries.size()) * Grain;
    std::size_t const end = limit < count ? limit : count;
    std::size_t const elements = end > begin ? end - begin : 0;
    if (outputs != elements) [[unlikely]] {
        fatal("a scan of grains got {} outputs for the {} elements of its grains", outputs, elements);
    }
    T const* const first = first_of_(ViewDoor::key(), in);
    T const* const carry = first_of_(ViewDoor::key(), carries);
    for (std::size_t index = 0; index < carries.size(); ++index) {
        std::size_t const grain = first_grain + index;
        std::size_t const start = grain * Grain;
        std::size_t const size = count - start < Grain ? count - start : Grain;
        scan_one(first + start, target + (start - begin), size, grain == 0 ? nullptr : carry + index);
    }
}

template <ScanKind Kind, std::size_t Grain, class T, class Combine>
constexpr void scan_grains_(View<T const> in, std::size_t first_grain, View<T const> carries, T* target,
                            std::size_t outputs, T const* empty, Combine& combine) noexcept {
    for_each_scan_grain_<Grain>(
        in, first_grain, carries, target, outputs,
        [empty, &combine](T const* first, T* part, std::size_t size, T const* carry) constexpr noexcept {
            scan_run_<Kind>(first, part, size, carry, empty, combine);
        });
}

}  // namespace detail

// The inclusive scan of the grains first_grain to first_grain +
// carries.size() - 1 of in.  out holds the outputs of the elements of these
// grains: the elements from the first element of grain first_grain to the
// end of the last grain of the range or of the run.  A parallel caller gives
// each shard the part of its grains of a split of the output.  carries[i] is
// the carry F(g * Grain) of the grain g = first_grain + i: the exclusive scan
// of the tree sums of the grains gives it.  The carry of grain 0 is not read.
// The scan of each grain of the run then gives the bits of inclusive_scan of
// the run.  A range past the grains of the run, or an output of a different
// count, ends the process.  in can be the View of a run whose elements from
// grain first_grain are the run of out.  Complexity: at most 2 * Grain
// combines for each grain.
template <std::size_t Grain, class T, class Brand, std::size_t Extent, class Combine>
    requires TreeGrain<Grain> && TreeValue<T> && TreeCombine<Combine, T> && std::is_nothrow_copy_assignable_v<T>
constexpr void inclusive_scan_grains(View<T const> in, std::size_t first_grain, View<T const> carries,
                                     ViewMut<T, Brand, Extent> out, Combine combine) noexcept {
    detail::scan_grains_<ScanKind::Inclusive, Grain>(in, first_grain, carries, first_of_(detail::ViewDoor::key(), out),
                                                     out.size(), static_cast<T const*>(nullptr), combine);
}

// The exclusive scan of the grains of the range, with the outputs and the
// carries of inclusive_scan_grains.  out[0] of the run is empty.
template <std::size_t Grain, class T, class Brand, std::size_t Extent, class Combine>
    requires TreeGrain<Grain> && TreeValue<T> && TreeCombine<Combine, T> && std::is_nothrow_copy_assignable_v<T>
constexpr void exclusive_scan_grains(View<T const> in, std::size_t first_grain, View<T const> carries,
                                     ViewMut<T, Brand, Extent> out, T const& empty, Combine combine) noexcept {
    detail::scan_grains_<ScanKind::Exclusive, Grain>(in, first_grain, carries, first_of_(detail::ViewDoor::key(), out),
                                                     out.size(), &empty, combine);
}

inline namespace CRUCIBLE_SIMD_TARGET {

// The sum scans of the grains of the range, with the lanes of one register
// of the target.  The outputs and the carries obey the rules of
// inclusive_scan_grains, and the bits are the bits of inclusive_sum_scan and
// exclusive_sum_scan of the run.
template <std::size_t Grain, class T, class Brand, std::size_t Extent>
    requires TreeGrain<Grain> && TreeSumElement<T>
void inclusive_sum_scan_grains(View<T const> in, std::size_t first_grain, View<T const> carries,
                               ViewMut<T, Brand, Extent> out) noexcept {
    detail::for_each_scan_grain_<Grain>(in, first_grain, carries, first_of_(detail::ViewDoor::key(), out), out.size(),
                                        [](T const* first, T* part, std::size_t size, T const* carry) noexcept {
                                            detail::sum_scan_lanes_<ScanKind::Inclusive, T, simd::native_lanes<T>>(
                                                first, part, size, carry);
                                        });
}

template <std::size_t Grain, class T, class Brand, std::size_t Extent>
    requires TreeGrain<Grain> && TreeSumElement<T>
void exclusive_sum_scan_grains(View<T const> in, std::size_t first_grain, View<T const> carries,
                               ViewMut<T, Brand, Extent> out) noexcept {
    detail::for_each_scan_grain_<Grain>(in, first_grain, carries, first_of_(detail::ViewDoor::key(), out), out.size(),
                                        [](T const* first, T* part, std::size_t size, T const* carry) noexcept {
                                            detail::sum_scan_lanes_<ScanKind::Exclusive, T, simd::native_lanes<T>>(
                                                first, part, size, carry);
                                        });
}

}  // namespace CRUCIBLE_SIMD_TARGET

// ── The plan of the scan ────────────────────────────────────────────────

// The node of an output that combines no element: out[0] of an exclusive
// scan.
inline constexpr PlanNode empty_plan_node{0xFFFFFFFFu};

namespace detail {

// The writer of a scan plan: the steps so far, and the node of each output.
// A null steps pointer counts the steps and writes nothing.
struct ScanPlanWriter {
    TreeStep* steps = nullptr;
    PlanNode* outputs = nullptr;
    std::uint32_t leaves = 0;
    std::uint32_t next = 0;

    [[nodiscard]] constexpr PlanNode combine(PlanNode left, PlanNode right) noexcept {
        if (steps != nullptr) steps[next] = TreeStep{left, right};
        PlanNode const node{leaves + next};
        ++next;
        return node;
    }

    constexpr void output(std::uint32_t position, PlanNode node) noexcept {
        if (outputs != nullptr) outputs[position] = node;
    }
};

// The plan of scan_node_: the same recursion, with the nodes of a plan in
// place of the values.
template <ScanKind Kind>
[[nodiscard]] constexpr PlanNode plan_scan_node_(ScanPlanWriter& writer, std::uint32_t position, std::uint32_t count,
                                                 PlanNode carry, bool is_sum_needed) noexcept {
    bool const has_carry = carry != empty_plan_node;
    if (count == 1) {
        PlanNode const value{position};
        if constexpr (Kind == ScanKind::Inclusive) {
            writer.output(position, has_carry ? writer.combine(carry, value) : value);
        } else {
            writer.output(position, carry);
        }
        return value;
    }
    if (count == 2) {
        PlanNode const first{position};
        PlanNode const second{position + 1};
        PlanNode const between = has_carry ? writer.combine(carry, first) : first;
        if constexpr (Kind == ScanKind::Inclusive) {
            PlanNode const last = writer.combine(between, second);
            writer.output(position, between);
            writer.output(position + 1, last);
            if (!has_carry) return last;
        } else {
            writer.output(position, carry);
            writer.output(position + 1, between);
        }
        return is_sum_needed ? writer.combine(first, second) : first;
    }
    std::uint32_t const split = bit_floor(count - 1u);
    PlanNode const left = plan_scan_node_<Kind>(writer, position, split, carry, true);
    PlanNode const right_carry = has_carry ? writer.combine(carry, left) : left;
    PlanNode const right = plan_scan_node_<Kind>(writer, position + split, count - split, right_carry, is_sum_needed);
    return is_sum_needed ? writer.combine(left, right) : left;
}

}  // namespace detail

// The count of the steps of the plan of a scan of count elements.  A count
// past max_plan_leaves gives TooManyLeaves.  Complexity: O(n).
[[nodiscard]] constexpr Result<std::size_t, PlanError> scan_plan_steps(std::size_t count, ScanKind kind) noexcept {
    if (count >= max_plan_leaves) return err(PlanError::TooManyLeaves);
    if (count == 0) return std::size_t{0};
    detail::ScanPlanWriter writer{nullptr, nullptr, static_cast<std::uint32_t>(count), 0};
    auto const leaves = static_cast<std::uint32_t>(count);
    if (kind == ScanKind::Inclusive) {
        static_cast<void>(detail::plan_scan_node_<ScanKind::Inclusive>(writer, 0, leaves, empty_plan_node, false));
    } else {
        static_cast<void>(detail::plan_scan_node_<ScanKind::Exclusive>(writer, 0, leaves, empty_plan_node, false));
    }
    return std::size_t{writer.next};
}

// Writes the plan of a scan of outputs.size() elements: the steps, and the
// node of each output.  outputs[k] is a leaf, the node of a step, or
// empty_plan_node for out[0] of an exclusive scan.  Each step comes after
// the steps of its operands, and each step feeds an output or a later step.
// The steps hold scan_plan_steps(n, kind) elements, and a different count
// gives CountMismatch with no write.  Complexity: O(n).
template <class StepBrand, std::size_t StepExtent, class OutputBrand, std::size_t OutputExtent>
[[nodiscard]] constexpr Result<Unit, PlanError>
write_scan_plan(ScanKind kind, ViewMut<TreeStep, StepBrand, StepExtent> steps,
                ViewMut<PlanNode, OutputBrand, OutputExtent> outputs) noexcept {
    auto counted = scan_plan_steps(outputs.size(), kind);
    if (counted.is_err()) return err(PlanError::TooManyLeaves);
    if (steps.size() != std::move(counted).expect("the count of a plan of fewer leaves than the limit")) {
        return err(PlanError::CountMismatch);
    }
    if (outputs.size() == 0) return Unit{};
    auto const leaves = static_cast<std::uint32_t>(outputs.size());
    detail::ScanPlanWriter writer{first_of_(detail::ViewDoor::key(), steps),
                                  first_of_(detail::ViewDoor::key(), outputs), leaves, 0};
    if (kind == ScanKind::Inclusive) {
        static_cast<void>(detail::plan_scan_node_<ScanKind::Inclusive>(writer, 0, leaves, empty_plan_node, false));
    } else {
        static_cast<void>(detail::plan_scan_node_<ScanKind::Exclusive>(writer, 0, leaves, empty_plan_node, false));
    }
    return Unit{};
}

// The outputs of a scan plan over the leaves, with the combine of the
// caller.  The scratch holds the value of each step, and out the value of
// each output.  An output of empty_plan_node takes empty.  The counts must
// agree: leaves, outputs and out hold one count, and scratch holds the count
// of the steps.  A step or an output that reads a node of a later step gives
// NodeNotReady.  Complexity: O(steps + n) combines and copies.
template <class T, class ScratchBrand, std::size_t ScratchExtent, class OutBrand, std::size_t OutExtent, class Combine>
    requires TreeValue<T> && TreeCombine<Combine, T> && std::is_nothrow_copy_assignable_v<T>
[[nodiscard]] constexpr Result<Unit, PlanError>
evaluate_scan_plan(View<TreeStep const> steps, View<PlanNode const> outputs, View<T const> leaves,
                   ViewMut<T, ScratchBrand, ScratchExtent> scratch, ViewMut<T, OutBrand, OutExtent> out, T const& empty,
                   Combine combine) noexcept {
    std::size_t const count = leaves.size();
    if (outputs.size() != count || out.size() != count || scratch.size() != steps.size()) {
        return err(PlanError::CountMismatch);
    }
    TreeStep const* const step = first_of_(detail::ViewDoor::key(), steps);
    PlanNode const* const output = first_of_(detail::ViewDoor::key(), outputs);
    T const* const leaf = first_of_(detail::ViewDoor::key(), leaves);
    T* const value = first_of_(detail::ViewDoor::key(), scratch);
    T* const target = first_of_(detail::ViewDoor::key(), out);
    std::size_t const total = count + steps.size();
    for (std::size_t index = 0; index < steps.size(); ++index) {
        auto const left = static_cast<std::size_t>(step[index].left);
        auto const right = static_cast<std::size_t>(step[index].right);
        if (left >= count + index || right >= count + index) return err(PlanError::NodeNotReady);
        T const& left_value = left < count ? leaf[left] : value[left - count];
        T const& right_value = right < count ? leaf[right] : value[right - count];
        value[index] = combine(left_value, right_value);
    }
    for (std::size_t position = 0; position < count; ++position) {
        if (output[position] == empty_plan_node) {
            target[position] = empty;
            continue;
        }
        auto const node = static_cast<std::size_t>(output[position]);
        if (node >= total) return err(PlanError::NodeNotReady);
        target[position] = node < count ? leaf[node] : value[node - count];
    }
    return Unit{};
}

}  // namespace foundation::core
