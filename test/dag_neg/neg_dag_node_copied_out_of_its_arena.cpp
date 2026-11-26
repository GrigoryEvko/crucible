// Negative compile test.  This file must fail to compile.
//
// Only a DAG arena builds a node (crucible/MerkleDag.h).  A copy of a node
// would be a second node outside each arena, with the brand of the DAG arenas,
// so a node does not copy.  A walk links the node instead.

#include <crucible/MerkleDag.h>

#include <cstddef>

int main() {
    ::fixy::BumpArena<crucible::DagArena> arena =
        ::fixy::mint_bump_arena<crucible::DagArena>(::fixy::Quantity<::fixy::unit::Byte, std::size_t>{4096});
    const crucible::DagRef<crucible::TraceNode> node =
        ::foundation::core::with_arena_frame(arena, [](auto& frame) noexcept {
            return crucible::mint_dag_node<crucible::TraceNode>(::foundation::effects::testing::test().alloc, frame)
                .weaken();
        });
    crucible::TraceNode copy{*node};
    return static_cast<int>(copy.kind);
}
