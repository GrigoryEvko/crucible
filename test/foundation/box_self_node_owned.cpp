// The first question of this unit is the transfer walk of SendableOwned
// (box_self_node.h).

#include "box_self_node.h"

namespace box_self_node {

static_assert(::foundation::transfer::SendableOwned<ListNode> && ::foundation::transfer::SendableOwned<TreeNode>);

using BuildAfterOwned = decltype(build_nodes<ListNode, TreeNode>());

}  // namespace box_self_node
