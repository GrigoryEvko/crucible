// Negative compile test.  This file must fail to compile.
//
// mint_dag_node builds a node of the DAG: TraceNode or a class that derives
// from it (crucible/MerkleDag.h).  A memory plan is no node, so its key and
// its brand do not come from the door of a node.

#include <crucible/MerkleDag.h>

#include <cstddef>

int main() {
    ::fixy::BumpArena<crucible::DagArena> arena =
        ::fixy::mint_bump_arena<crucible::DagArena>(::fixy::Quantity<::fixy::unit::Byte, std::size_t>{4096});
    // The body states its result, so the door reads no line of the body, and
    // the mint is the one refused line.
    ::foundation::core::with_arena_frame(arena, [](auto& frame) noexcept -> void {
        const auto plan =
            crucible::mint_dag_node<crucible::MemoryPlan>(::foundation::effects::testing::test().alloc, frame);
        static_cast<void>(plan->pool_bytes);
    });
    return 0;
}
