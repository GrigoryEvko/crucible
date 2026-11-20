// The first question of this unit is the build of the nodes, which asks the
// constraints of the assignment of a Box (box_self_node.h).

#include "box_self_node.h"

namespace box_self_node {

using BuildFirst = decltype(build_nodes<ListNode, TreeNode>());

}  // namespace box_self_node
