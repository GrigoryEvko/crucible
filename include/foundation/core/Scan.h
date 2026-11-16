#pragma once

// The prefix scans of the Region family, in one stated order.  The result
// has the same bits for each thread count, each schedule and each host.
//
//   inclusive_scan(in, out, combine)         out[k] combines in[0] to in[k]
//   exclusive_scan(in, out, empty, combine)  out[k] combines in[0] to
//                                            in[k - 1], and out[0] is empty
//   inclusive_scan_grains<G>(...), exclusive_scan_grains<G>(...)
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
//   and u = 2^-24 for float and 2^-53 for double.  THE VALUES OUTSIDE THE
//   BOUND of Reduce.h applies to a NaN and to an overflow.
//
// THE COSTS
//   A scan makes at most two combines for each element, with a recursion of
//   depth ceil(log2 n) + popcount(n).  It allocates nothing.  out can be the
//   same View as in, and no other overlap is permitted.

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
// caller.  The two Views hold one count, and two counts that differ give
// LengthMismatch with no write.  out can be the same View as in.
// Complexity: at most 2n combines.
template <class T, class Combine>
    requires TreeValue<T> && TreeCombine<Combine, T> && std::is_nothrow_copy_assignable_v<T>
[[nodiscard]] constexpr Result<Unit, LengthMismatch> inclusive_scan(View<T const> in, View<T> out,
                                                                    Combine combine) noexcept {
    if (in.size() != out.size()) return err(LengthMismatch{});
    detail::scan_run_<ScanKind::Inclusive>(first_of_(detail::ViewDoor::key(), in),
                                           first_of_(detail::ViewDoor::key(), out), in.size(),
                                           static_cast<T const*>(nullptr), static_cast<T const*>(nullptr), combine);
    return Unit{};
}

// The exclusive scan of in into out, by THE ORDER: out[0] is empty, and
// out[k] combines in[0] to in[k - 1].  The two Views hold one count, and two
// counts that differ give LengthMismatch with no write.  Complexity: at most
// 2n combines.
template <class T, class Combine>
    requires TreeValue<T> && TreeCombine<Combine, T> && std::is_nothrow_copy_assignable_v<T>
[[nodiscard]] constexpr Result<Unit, LengthMismatch> exclusive_scan(View<T const> in, View<T> out, T const& empty,
                                                                    Combine combine) noexcept {
    if (in.size() != out.size()) return err(LengthMismatch{});
    detail::scan_run_<ScanKind::Exclusive>(first_of_(detail::ViewDoor::key(), in),
                                           first_of_(detail::ViewDoor::key(), out), in.size(),
                                           static_cast<T const*>(nullptr), &empty, combine);
    return Unit{};
}

// The sum scans: the scans with the add of a tree sum.  The empty value of
// the exclusive sum is +0.
template <class T>
    requires TreeSumElement<T>
[[nodiscard]] constexpr Result<Unit, LengthMismatch> inclusive_sum_scan(View<T const> in, View<T> out) noexcept {
    return inclusive_scan(in, out, detail::TreeAdd{});
}

template <class T>
    requires TreeSumElement<T>
[[nodiscard]] constexpr Result<Unit, LengthMismatch> exclusive_sum_scan(View<T const> in, View<T> out) noexcept {
    return exclusive_scan(in, out, T{}, detail::TreeAdd{});
}

// ── The split into grains ───────────────────────────────────────────────

namespace detail {

template <ScanKind Kind, std::size_t Grain, class T, class Combine>
constexpr void scan_grains_(View<T const> in, std::size_t first_grain, View<T const> carries, View<T> out,
                            T const* empty, Combine& combine) noexcept {
    std::size_t const count = in.size();
    if (out.size() != count) [[unlikely]] {
        fatal("a scan of grains got an input of {} elements and an output of {}", count, out.size());
    }
    check_grain_range_<Grain>(count, first_grain, carries.size());
    T const* const first = first_of_(ViewDoor::key(), in);
    T* const target = first_of_(ViewDoor::key(), out);
    T const* const carry = first_of_(ViewDoor::key(), carries);
    for (std::size_t index = 0; index < carries.size(); ++index) {
        std::size_t const grain = first_grain + index;
        std::size_t const start = grain * Grain;
        std::size_t const size = count - start < Grain ? count - start : Grain;
        scan_run_<Kind>(first + start, target + start, size, grain == 0 ? nullptr : carry + index, empty, combine);
    }
}

}  // namespace detail

// The inclusive scan of the grains first_grain to first_grain +
// carries.size() - 1 of in, into the same positions of out.  carries[i] is
// the carry F(g * Grain) of the grain g = first_grain + i: the exclusive scan
// of the tree sums of the grains gives it.  The carry of grain 0 is not read.
// The scan of each grain of the run then gives the bits of inclusive_scan of
// the run.  A range past the grains of the run, or an output of a different
// count, ends the process.  Complexity: at most 2 * Grain combines for each
// grain.
template <std::size_t Grain, class T, class Combine>
    requires TreeGrain<Grain> && TreeValue<T> && TreeCombine<Combine, T> && std::is_nothrow_copy_assignable_v<T>
constexpr void inclusive_scan_grains(View<T const> in, std::size_t first_grain, View<T const> carries, View<T> out,
                                     Combine combine) noexcept {
    detail::scan_grains_<ScanKind::Inclusive, Grain>(in, first_grain, carries, out, static_cast<T const*>(nullptr),
                                                     combine);
}

// The exclusive scan of the grains of the range, with the carries of
// inclusive_scan_grains.  out[0] of the run is empty.
template <std::size_t Grain, class T, class Combine>
    requires TreeGrain<Grain> && TreeValue<T> && TreeCombine<Combine, T> && std::is_nothrow_copy_assignable_v<T>
constexpr void exclusive_scan_grains(View<T const> in, std::size_t first_grain, View<T const> carries, View<T> out,
                                     T const& empty, Combine combine) noexcept {
    detail::scan_grains_<ScanKind::Exclusive, Grain>(in, first_grain, carries, out, &empty, combine);
}

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
[[nodiscard]] constexpr Result<Unit, PlanError> write_scan_plan(ScanKind kind, View<TreeStep> steps,
                                                                View<PlanNode> outputs) noexcept {
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
template <class T, class Combine>
    requires TreeValue<T> && TreeCombine<Combine, T> && std::is_nothrow_copy_assignable_v<T>
[[nodiscard]] constexpr Result<Unit, PlanError>
evaluate_scan_plan(View<TreeStep const> steps, View<PlanNode const> outputs, View<T const> leaves, View<T> scratch,
                   View<T> out, T const& empty, Combine combine) noexcept {
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
