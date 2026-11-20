// The first question of this unit is the linearity trait (box_self_node.h).

#include "box_self_node.h"

namespace box_self_node {

static_assert(!::foundation::linear::is_linear_type(^^ListNode) && !::foundation::linear::is_linear_type(^^TreeNode));

using BuildAfterLinear = decltype(build_nodes<ListNode, TreeNode>());

}  // namespace box_self_node
