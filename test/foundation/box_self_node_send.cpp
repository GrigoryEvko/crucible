// The first question of this unit is the transfer walk of Sendable
// (box_self_node.h).

#include "box_self_node.h"

namespace box_self_node {

static_assert(::foundation::transfer::Sendable<ListNode> && ::foundation::transfer::Sendable<TreeNode>);

using BuildAfterSend = decltype(build_nodes<ListNode, TreeNode>());

}  // namespace box_self_node
