// Negative compile test.  This file must fail to compile.
//
// Only mint_dag_node builds a node, in a DAG arena (crucible/MerkleDag.h).  A
// copy of a node would be a second node outside each arena, with the brand of
// the DAG arenas, so a node does not copy.  A walk links the node instead.

#include <crucible/MerkleDag.h>

#include <cstddef>

int main() {
    ::fixy::BumpArena<crucible::DagArena> arena =
        ::fixy::mint_bump_arena<crucible::DagArena>(::fixy::Quantity<::fixy::unit::Byte, std::size_t>{4096});
    const crucible::DagRef<crucible::TraceNode> node =
        crucible::mint_dag_node<crucible::TraceNode>(::foundation::effects::testing::test().alloc, arena);
    crucible::TraceNode copy{*node};
    return static_cast<int>(copy.kind);
}
