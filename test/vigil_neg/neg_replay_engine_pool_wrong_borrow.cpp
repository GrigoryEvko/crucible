// Negative-compile test.  This file must fail to compile.
//
// ReplayEngine::init requires a Ref of the PoolAllocator.  A fixy::Ref of
// another owner type cannot substitute, even though each Ref has the size of
// a pointer.
//
// Expected diagnostic: no init overload accepting a Ref of a RegionNode.

#include <crucible/ReplayEngine.h>

int main() {
    ::fixy::BumpArena<crucible::DagArena> arena = ::fixy::mint_bump_arena<crucible::DagArena>(
        ::fixy::Quantity<::fixy::unit::Byte, std::size_t>{std::size_t{4096}});
    const crucible::DagRef<crucible::RegionNode> region =
        crucible::mint_dag_node<crucible::RegionNode>(::foundation::effects::testing::test().alloc, arena);
    crucible::ReplayEngine engine{};
    engine.init(*region, region);
    return 0;
}
