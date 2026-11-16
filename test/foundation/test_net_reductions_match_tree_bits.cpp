// The match of the reductions library and its nets, part two: the bits of
// the tree sums.  sum_by_tree and its forms of 4, 8 and 16 lanes give the
// bits of the evaluation of the net of the tree plan on Philox draws.  The
// sum forms run on SIMD lanes at run time, so main runs these tests.  The
// nets come from reductions_net.h.
//
// A control shows that the bit test can refuse a wrong net: the net of a left
// fold gives bits that sum_by_tree does not give.

#include <foundation/core/Array.h>
#include <foundation/core/Reduce.h>
#include <foundation/core/Sort.h>

#include "../test_assert.h"
#include "philox_stream.h"
#include "reductions_net.h"

#include <cstddef>
#include <cstdint>

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

}  // namespace

int main() {
    bool const is_tree_float =
        does_tree_sum_follow<float, 6>(0x7E570101u) && does_tree_sum_follow<float, 13>(0x7E570102u)
        && does_tree_sum_follow<float, 37>(0x7E570103u) && does_tree_sum_follow<float, 64>(0x7E570104u);
    bool const is_tree_double =
        does_tree_sum_follow<double, 13>(0x7E570105u) && does_tree_sum_follow<double, 37>(0x7E570106u);
    bool const is_fold_apart =
        does_fold_differ_on_some<float, 37>(0x7E570501u) && does_fold_differ_on_some<double, 37>(0x7E570502u);
    assert(is_tree_float);
    assert(is_tree_double);
    assert(is_fold_apart);
    return 0;
}
