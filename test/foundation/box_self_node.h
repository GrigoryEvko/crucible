#pragma once

// The shared part of test_core_box_self_node: a list node and a tree node that
// hold a Box of their own type, and the build of each node.
//
// GCC keeps the results of constraints in a cache that its garbage collector
// empties.  A later read of the members of a class then checks the
// constraints of its member functions again, so a Box constraint that walks
// its payload again can reach the read that is still open.  Each unit of the
// test asks a different first question of the nodes, right after this
// header.  In the default configuration, the unit of the transfer walk
// compiles with a collection at each opportunity, so its result does not
// depend on when GCC collects.  A unit then reads the type of the build, and
// the deduction of that type instantiates the body of the build at that
// point, with no code.  main runs the build.

#include <foundation/Linear.h>
#include <foundation/Transfer.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

#include <utility>

namespace box_self_node {

struct ListNode {
    int value = 0;
    ::foundation::core::Option<::foundation::core::Box<ListNode>> next{};
};

struct TreeNode {
    int value = 0;
    ::foundation::core::Option<::foundation::core::Box<TreeNode>> left{};
    ::foundation::core::Option<::foundation::core::Box<TreeNode>> right{};
};

// The sum of the values of the nodes that the build links: 1 + 2 + 4 + 8.
inline constexpr int linked_value_sum = 15;

// Builds a list of two nodes and a tree of two nodes, assigns a Box of a
// node, and gives the sum of the values of the linked nodes.  The build comes
// first in the body, and the facts of the nodes after it: no node is linear,
// and each node moves to another thread.  The type of the result is deduced,
// so a read of the type instantiates the body.
template <class List, class Tree>
[[nodiscard]] auto build_nodes() noexcept {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    ::foundation::core::Box<List> tail = ::foundation::core::mint_box<List>(alloc);
    tail = ::foundation::core::with_mut(std::move(tail), [](List& node) noexcept { node.value = 2; });
    List head{};
    head.value = 1;
    head.next = ::foundation::core::Option<::foundation::core::Box<List>>::some(std::move(tail));

    // The assignment ends the node that the target holds.
    ::foundation::core::Box<List> target = ::foundation::core::mint_box<List>(alloc);
    target = ::foundation::core::mint_box<List>(alloc);
    target = ::foundation::core::with_mut(std::move(target), [](List& node) noexcept { node.value = 8; });

    ::foundation::core::Box<Tree> leaf = ::foundation::core::mint_box<Tree>(alloc);
    leaf = ::foundation::core::with_mut(std::move(leaf), [](Tree& node) noexcept { node.value = 4; });
    Tree root{};
    root.left = ::foundation::core::Option<::foundation::core::Box<Tree>>::some(std::move(leaf));

    const bool is_list_linked = head.next.is_some_and([](::foundation::core::Box<List> const& next) noexcept {
        return ::foundation::core::with_view(next, [](List const& node) noexcept { return node.value == 2; });
    });
    const bool is_tree_linked = root.left.is_some_and([](::foundation::core::Box<Tree> const& left) noexcept {
        return ::foundation::core::with_view(left, [](Tree const& node) noexcept { return node.value == 4; });
    });

    static_assert(!::foundation::linear::is_linear_type(^^List) && !::foundation::linear::is_linear_type(^^Tree));
    static_assert(::foundation::core::DroppablePayload<::foundation::core::Box<List>>);
    static_assert(::foundation::transfer::Sendable<List> && ::foundation::transfer::SendableOwned<List>
                  && ::foundation::transfer::SendableBeside<List>);
    static_assert(::foundation::transfer::Sendable<Tree> && ::foundation::transfer::SendableOwned<Tree>);
    int const target_value =
        ::foundation::core::with_view(target, [](List const& node) noexcept { return node.value; });
    return head.value + (is_list_linked ? 2 : 0) + (is_tree_linked ? 4 : 0) + target_value;
}

}  // namespace box_self_node
