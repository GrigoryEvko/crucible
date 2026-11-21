// The match of the reductions library and its nets, part two: the bits of
// the tree sums.  sum_by_tree and its forms of 4, 8 and 16 lanes,
// sum_grains_by_tree, sum_runs_by_tree and sum_segments_by_tree give the bits
// of the evaluation of the net of the tree plan on Philox draws.  The sum
// forms run on SIMD lanes at run time, so main runs these tests.  The nets
// come from reductions_net.h.
//
// A control shows that the bit test can refuse a wrong net: the net of a left
// fold gives bits that sum_by_tree does not give.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/core/Array.h>
#include <foundation/core/Reduce.h>
#include <foundation/core/Sort.h>
#include <foundation/effects/Effect.h>

#include "philox_stream.h"
#include "reductions_net.h"

#include <cstddef>
#include <cstdint>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace {

using namespace ::crucible::test::reductions_net;

// sum_by_tree and its forms of 4, 8 and 16 lanes give the bits of the net of
// the tree plan, on 16 draws.
template <class T, std::size_t Leaves>
[[nodiscard]] bool does_tree_sum_follow(std::uint64_t seed) noexcept {
    ::crucible::test::PhiloxStream seeds{seed};
    bool is_each = true;
    for (std::uint32_t round = 0; round < 16; ++round) {
        auto const leaves = draw_values<T, Leaves>(seeds.next_wide());
        T const from_net =
            slot_at(evaluate_net<1>(tree_net_of<Leaves, false>, leaves, T{}, AddValues{}, core::Ascending{}), 0);
        core::View<T const> const run{leaves.view()};
        is_each = is_each && has_same_bits(from_net, core::sum_by_tree(run))
               && has_same_bits(from_net, core::sum_by_tree_in_lanes<4>(run))
               && has_same_bits(from_net, core::sum_by_tree_in_lanes<8>(run))
               && has_same_bits(from_net, core::sum_by_tree_in_lanes<16>(run));
    }
    return is_each;
}

// The net of the left fold gives bits that sum_by_tree does not give, on
// some of 64 draws, so the bit test tells the two plans apart.  A few large
// elements can rule a sum, and then the two plans round to the same bits.
// So one draw is not sufficient.  On the float draws of the seed of main,
// the bits differ on 45 of the 64 draws.
template <class T, std::size_t Leaves>
[[nodiscard]] bool does_fold_differ_on_some(std::uint64_t seed) noexcept {
    ::crucible::test::PhiloxStream seeds{seed};
    bool is_apart = false;
    for (std::uint32_t round = 0; round < 64; ++round) {
        auto const leaves = draw_values<T, Leaves>(seeds.next_wide());
        T const from_fold =
            slot_at(evaluate_net<1>(fold_net_of<Leaves>, leaves, T{}, AddValues{}, core::Ascending{}), 0);
        is_apart = is_apart || !has_same_bits(from_fold, core::sum_by_tree(core::View<T const>{leaves.view()}));
    }
    return is_apart;
}

// The value of the tree net of Count leaves on the elements of a run from
// the position First.
template <std::size_t Count, std::size_t First, class T, std::size_t Leaves>
    requires(First + Count <= Leaves)
[[nodiscard]] T tree_of_part(core::FixedArray<T, Leaves> const& leaves) noexcept {
    core::FixedArray<T, Count> part{};
    for (std::size_t index = 0; index < Count; ++index) {
        T const element = slot_at(leaves, First + index);
        part.with_element(slot_index(part, index), [element](T& held) noexcept { held = element; });
    }
    return slot_at(evaluate_net<1>(tree_net_of<Count, false>, part, T{}, AddValues{}, core::Ascending{}), 0);
}

// sum_grains_by_tree of 37 leaves in grains of 8 gives the bits of the tree
// net of 8 leaves for the first four grains and of 5 leaves for the last.
// sum_by_tree of the five partials gives the bits of the tree net of the
// run.  On 16 draws.
template <class T>
[[nodiscard]] bool does_grain_sum_follow(std::uint64_t seed) noexcept {
    ::crucible::test::PhiloxStream seeds{seed};
    bool is_each = true;
    for (std::uint32_t round = 0; round < 16; ++round) {
        auto const leaves = draw_values<T, 37>(seeds.next_wide());
        core::FixedArray<T, 5> partials{};
        partials.with_mut([&leaves](auto out) noexcept {
            core::sum_grains_by_tree<8>(core::View<T const>{leaves.view()}, 0, std::move(out));
        });
        is_each = is_each && has_same_bits(slot_at(partials, 0), tree_of_part<8, 0>(leaves))
               && has_same_bits(slot_at(partials, 1), tree_of_part<8, 8>(leaves))
               && has_same_bits(slot_at(partials, 2), tree_of_part<8, 16>(leaves))
               && has_same_bits(slot_at(partials, 3), tree_of_part<8, 24>(leaves))
               && has_same_bits(slot_at(partials, 4), tree_of_part<5, 32>(leaves));
        is_each = is_each
               && has_same_bits(core::sum_by_tree(core::View<T const>{partials.view()}), tree_of_part<37, 0>(leaves));
    }
    return is_each;
}

// sum_runs_by_tree of Runs runs of 37 elements gives at each position the bits
// of the tree net of Runs leaves over the elements of the runs at that
// position, in run order.  On 16 draws.
template <class T, std::size_t Runs>
[[nodiscard]] bool does_runs_sum_follow(std::uint64_t seed) noexcept {
    ::crucible::test::PhiloxStream seeds{seed};
    bool is_each = true;
    for (std::uint32_t round = 0; round < 16; ++round) {
        core::FixedArray<core::FixedArray<T, 37>, Runs> runs{};
        runs.with_mut([&seeds](auto each_run) noexcept {
            for (core::FixedArray<T, 37>& run : each_run)
                run = draw_values<T, 37>(seeds.next_wide());
        });
        core::FixedArray<core::View<T const>, Runs> views{};
        std::size_t index = 0;
        for (core::FixedArray<T, 37> const& run : runs) {
            core::View<T const> const run_view{run.view()};
            views.with_element(slot_index(views, index),
                               [run_view](core::View<T const>& held) noexcept { held = run_view; });
            ++index;
        }
        core::FixedArray<T, 37> sums{};
        bool const is_summed = sums.with_mut([&views](auto out) noexcept {
            return core::sum_runs_by_tree(core::View<core::View<T const> const>{views.view()}, std::move(out)).is_ok();
        });
        is_each = is_each && is_summed;
        for (std::size_t position = 0; position < 37; ++position) {
            core::FixedArray<T, Runs> column{};
            for (std::size_t run = 0; run < Runs; ++run) {
                T const element = slot_at(slot_at(runs, run), position);
                column.with_element(slot_index(column, run), [element](T& held) noexcept { held = element; });
            }
            T const from_net =
                slot_at(evaluate_net<1>(tree_net_of<Runs, false>, column, T{}, AddValues{}, core::Ascending{}), 0);
            is_each = is_each && has_same_bits(slot_at(sums, position), from_net);
        }
    }
    return is_each;
}

// sum_segments_by_tree of 50 elements at the offsets 0, 6, 6, 43 and 50
// gives the bits of the tree nets of 6, 37 and 7 leaves, and +0 for the empty
// segment.  On 16 draws.
template <class T>
[[nodiscard]] bool does_segment_sum_follow(std::uint64_t seed) noexcept {
    ::crucible::test::PhiloxStream seeds{seed};
    core::FixedArray<std::size_t, 5> const offsets{0, 6, 6, 43, 50};
    bool is_each = true;
    for (std::uint32_t round = 0; round < 16; ++round) {
        auto const elements = draw_values<T, 50>(seeds.next_wide());
        core::FixedArray<T, 4> sums{};
        bool const is_summed = sums.with_mut([&elements, &offsets](auto out) noexcept {
            return core::sum_segments_by_tree(core::View<T const>{elements.view()},
                                              core::View<std::size_t const>{offsets.view()}, std::move(out))
                .is_ok();
        });
        is_each = is_each && is_summed && has_same_bits(slot_at(sums, 0), tree_of_part<6, 0>(elements))
               && has_same_bits(slot_at(sums, 1), T{}) && has_same_bits(slot_at(sums, 2), tree_of_part<37, 6>(elements))
               && has_same_bits(slot_at(sums, 3), tree_of_part<7, 43>(elements));
    }
    return is_each;
}

}  // namespace

int main() {
    bool const is_tree_float =
        does_tree_sum_follow<float, 6>(0x7E570101u) && does_tree_sum_follow<float, 13>(0x7E570102u)
        && does_tree_sum_follow<float, 37>(0x7E570103u) && does_tree_sum_follow<float, 64>(0x7E570104u);
    bool const is_tree_double =
        does_tree_sum_follow<double, 13>(0x7E570105u) && does_tree_sum_follow<double, 37>(0x7E570106u);
    bool const is_fold_apart =
        does_fold_differ_on_some<float, 37>(0x7E570501u) && does_fold_differ_on_some<double, 37>(0x7E570502u);
    bool const is_grain = does_grain_sum_follow<float>(0x7E570601u) && does_grain_sum_follow<double>(0x7E570602u);
    bool const is_runs = does_runs_sum_follow<float, 6>(0x7E570701u) && does_runs_sum_follow<float, 13>(0x7E570702u)
                      && does_runs_sum_follow<double, 6>(0x7E570703u);
    bool const is_segment = does_segment_sum_follow<float>(0x7E570801u) && does_segment_sum_follow<double>(0x7E570802u);
    CRUCIBLE_TEST_ASSERT(kTest, is_tree_float);
    CRUCIBLE_TEST_ASSERT(kTest, is_tree_double);
    CRUCIBLE_TEST_ASSERT(kTest, is_fold_apart);
    CRUCIBLE_TEST_ASSERT(kTest, is_grain);
    CRUCIBLE_TEST_ASSERT(kTest, is_runs);
    CRUCIBLE_TEST_ASSERT(kTest, is_segment);
    return 0;
}
