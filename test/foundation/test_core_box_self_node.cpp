// A node that holds a Box of its own type compiles and builds, in each order
// of the first question that a unit asks of it (box_self_node.h).  In the
// default configuration, the unit of the transfer walk compiles with a
// collection of the garbage of GCC at each opportunity, so the cache of the
// results of constraints is empty at each question of that unit.  This unit
// runs the build.

#include "box_self_node.h"

#include <foundation/Platform.h>

int main() {
    CRUCIBLE_FATAL_INVARIANT((::box_self_node::build_nodes<::box_self_node::ListNode, ::box_self_node::TreeNode>()
                              == ::box_self_node::linked_value_sum));
    return 0;
}
