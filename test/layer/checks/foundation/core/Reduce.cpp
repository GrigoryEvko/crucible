// The compile-time checks of foundation/core/Reduce.h.

#include <foundation/core/Reduce.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace foundation::core {

namespace detail::reduce_checks {

// A combine that is neither associative nor commutative: a change of the
// shape of the tree or of the order of two operands changes its result.
struct Mix {
    [[nodiscard]] constexpr std::uint64_t operator()(std::uint64_t const& left,
                                                     std::uint64_t const& right) const noexcept {
        return rotl(left, 7) * 0x9E3779B97F4A7C15u + (right ^ 0xD1B54A32D192ED03u);
    }
};

// A combine that can throw, and a combine that gives another type.
struct ThrowingCombine {
    [[nodiscard]] int operator()(int const& left, int const& right) const { return left + right; }
};
struct WideningCombine {
    [[nodiscard]] long operator()(int const& left, int const& right) const noexcept { return long{left} + right; }
};

// The leaves keep plain values of at most 64 bytes that assign.  A value with
// a const member does not assign, and a large value would make the scratch of
// a leaf large, so the two take the recursion.
struct ConstLeaf {
    int const value = 0;
};
struct LargeLeaf {
    FixedArray<std::uint64_t, 9> words{};
};
struct SmallLeaf {
    std::uint32_t low = 0;
    std::uint32_t high = 0;
};
static_assert(TreeLeafValue<float> && TreeLeafValue<std::uint64_t> && TreeLeafValue<SmallLeaf>);
static_assert(TreeValue<ConstLeaf> && !TreeLeafValue<ConstLeaf>);
static_assert(TreeValue<LargeLeaf> && !TreeLeafValue<LargeLeaf>);
static_assert(std::is_trivially_copyable_v<LargeLeaf> && sizeof(LargeLeaf) > tree_leaf_value_bytes);

static_assert(TreeCombine<Mix, std::uint64_t>);
static_assert(TreeCombine<TreeAdd, float>);
static_assert(!TreeCombine<ThrowingCombine, int>);
static_assert(!TreeCombine<WideningCombine, int>);
static_assert(TreeSumElement<float> && TreeSumElement<double> && !TreeSumElement<int>);
static_assert(TreeGrain<1> && TreeGrain<4096> && !TreeGrain<0> && !TreeGrain<48>);

// The depth of the tree is ceil(log2 n).
static_assert(count_tree_depth(0) == 0 && count_tree_depth(1) == 0 && count_tree_depth(2) == 1
              && count_tree_depth(3) == 2);
static_assert(count_tree_depth(4) == 2 && count_tree_depth(5) == 3 && count_tree_depth(1024) == 10
              && count_tree_depth(1025) == 11);

// The count of the grains rounds up.
static_assert(count_tree_grains<4>(0) == 0 && count_tree_grains<4>(1) == 1 && count_tree_grains<4>(8) == 2);
static_assert(count_tree_grains<4>(9) == 3);

// The plan of three leaves is (x0, x1), then (that, x2).
constexpr auto plan_of_three = plan_tree<3>();
static_assert(plan_of_three.at(index<0>()) == TreeStep{PlanNode{0}, PlanNode{1}});
static_assert(plan_of_three.at(index<1>()) == TreeStep{PlanNode{3}, PlanNode{2}});

// The plan of six leaves is ((x0, x1), (x2, x3)), then (that, (x4, x5)).
constexpr auto plan_of_six = plan_tree<6>();
static_assert(plan_of_six.at(index<0>()) == TreeStep{PlanNode{0}, PlanNode{1}});
static_assert(plan_of_six.at(index<1>()) == TreeStep{PlanNode{2}, PlanNode{3}});
static_assert(plan_of_six.at(index<2>()) == TreeStep{PlanNode{6}, PlanNode{7}});
static_assert(plan_of_six.at(index<3>()) == TreeStep{PlanNode{4}, PlanNode{5}});
static_assert(plan_of_six.at(index<4>()) == TreeStep{PlanNode{8}, PlanNode{9}});

// The leaves 1, 2, 3, ... of a run of N.
template <std::size_t N>
[[nodiscard]] consteval FixedArray<std::uint64_t, N> leaves_of() noexcept {
    FixedArray<std::uint64_t, N> leaves{};
    std::uint64_t next = 1;
    for (std::uint64_t& leaf : leaves) {
        leaf = next++;
    }
    return leaves;
}

// The plan of N leaves gives the value of reduce_by_tree for the combine Mix.
template <std::size_t N>
[[nodiscard]] consteval bool plan_agrees_with_reduce() noexcept {
    auto const leaves = leaves_of<N>();
    auto const steps = plan_tree<N>();
    FixedArray<std::uint64_t, N - 1> scratch{};
    auto [from_plan, back] = with_mut(std::move(scratch), [&](auto room) noexcept {
        return evaluate_tree_plan(View<TreeStep const>{steps.view()}, View<std::uint64_t const>{leaves.view()},
                                  std::move(room), Mix{})
            .expect("the plan of plan_tree is ready in step order");
    });
    static_cast<void>(back);
    std::uint64_t const from_reduce = reduce_by_tree(leaves.view(), Mix{}).expect("the run has leaves");
    return from_plan == from_reduce;
}

template <std::size_t... N>
[[nodiscard]] consteval bool plans_agree(std::index_sequence<N...> /*counts*/) noexcept {
    return (plan_agrees_with_reduce<N + 2>() && ...);
}

static_assert(plans_agree(std::make_index_sequence<40>{}));
static_assert(plan_agrees_with_reduce<63>() && plan_agrees_with_reduce<64>() && plan_agrees_with_reduce<65>());
static_assert(plan_agrees_with_reduce<127>() && plan_agrees_with_reduce<128>() && plan_agrees_with_reduce<129>());
// The counts around the largest leaf of 256 elements, and a count whose
// blocks are 512, 256 and 8 elements: each leaf and the joins of the leaves.
static_assert(plan_agrees_with_reduce<255>() && plan_agrees_with_reduce<256>() && plan_agrees_with_reduce<257>());
static_assert(plan_agrees_with_reduce<511>() && plan_agrees_with_reduce<512>() && plan_agrees_with_reduce<513>());
static_assert(plan_agrees_with_reduce<776>());

// An empty View has no tree, and one element is its own tree.
static_assert(reduce_by_tree(View<std::uint64_t const>{}, Mix{}).is_none());
constexpr FixedArray<std::uint64_t, 1> one_leaf{7};
static_assert(reduce_by_tree(one_leaf.view(), Mix{}).expect("one leaf") == 7);

// A step that reads a later node is refused.
constexpr FixedArray<TreeStep, 1> bad_plan{TreeStep{PlanNode{0}, PlanNode{2}}};
[[nodiscard]] consteval bool bad_plan_is_refused() noexcept {
    FixedArray<std::uint64_t, 2> const two{leaves_of<2>()};
    FixedArray<std::uint64_t, 1> scratch{};
    auto [is_refused, back] = with_mut(std::move(scratch), [&](auto room) noexcept {
        return evaluate_tree_plan(View<TreeStep const>{bad_plan.view()}, View<std::uint64_t const>{two.view()},
                                  std::move(room), Mix{})
            .is_err();
    });
    static_cast<void>(back);
    return is_refused;
}
static_assert(bad_plan_is_refused());

// The segments of the leaves 1 to 6: [0, 2), [2, 2) and [2, 6).
[[nodiscard]] consteval bool segments_take_their_trees() noexcept {
    auto const leaves = leaves_of<6>();
    FixedArray<std::size_t, 4> const offsets{0, 2, 2, 6};
    FixedArray<std::uint64_t, 3> results{};
    auto [is_written, written] = with_mut(std::move(results), [&](auto out) noexcept {
        return reduce_segments_by_tree(View<std::uint64_t const>{leaves.view()},
                                       View<std::size_t const>{offsets.view()}, std::move(out), std::uint64_t{99},
                                       Mix{})
            .is_ok();
    });
    Mix mix{};
    std::uint64_t const tail = mix(mix(3, 4), mix(5, 6));
    return is_written && written.at(index<0>()) == mix(1, 2) && written.at(index<1>()) == 99
        && written.at(index<2>()) == tail;
}
static_assert(segments_take_their_trees());

// Offsets that decrease, or that end past the elements, are refused.
[[nodiscard]] consteval bool bad_segments_are_refused() noexcept {
    auto const leaves = leaves_of<6>();
    FixedArray<std::size_t, 3> const decreasing{0, 4, 2};
    FixedArray<std::size_t, 3> const past_end{0, 4, 7};
    FixedArray<std::uint64_t, 2> results{};
    auto [is_first_refused, once] = with_mut(std::move(results), [&](auto out) noexcept {
        return reduce_segments_by_tree(View<std::uint64_t const>{leaves.view()},
                                       View<std::size_t const>{decreasing.view()}, std::move(out), std::uint64_t{0},
                                       Mix{})
            .is_err();
    });
    auto [is_second_refused, twice] = with_mut(std::move(once), [&](auto out) noexcept {
        return reduce_segments_by_tree(View<std::uint64_t const>{leaves.view()},
                                       View<std::size_t const>{past_end.view()}, std::move(out), std::uint64_t{0},
                                       Mix{})
            .is_err();
    });
    static_cast<void>(twice);
    return is_first_refused && is_second_refused;
}
static_assert(bad_segments_are_refused());

}  // namespace detail::reduce_checks

}  // namespace foundation::core
