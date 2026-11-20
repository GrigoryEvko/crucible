// The first question of this unit is DroppablePayload of a Box of a node
// (box_self_node.h).

#include "box_self_node.h"

namespace box_self_node {

static_assert(::foundation::core::DroppablePayload<::foundation::core::Box<ListNode>>
              && ::foundation::core::DroppablePayload<::foundation::core::Box<TreeNode>>);

using BuildAfterDroppable = decltype(build_nodes<ListNode, TreeNode>());

}  // namespace box_self_node
