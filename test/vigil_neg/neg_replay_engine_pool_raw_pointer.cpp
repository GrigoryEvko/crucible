// Negative-compile test.  This file must fail to compile.
//
// ReplayEngine::init takes the pool as a fixy::Ref of a PoolAllocator.  A
// raw PoolAllocator* must not cross the init boundary.
//
// Expected diagnostic: no init overload accepting PoolAllocator*.

#include <crucible/ReplayEngine.h>
#include <foundation/Quarantine.h>

int main() {
    ::fixy::BumpArena<crucible::DagArena> arena = ::fixy::mint_bump_arena<crucible::DagArena>(
        ::fixy::Quantity<::fixy::unit::Byte, std::size_t>{std::size_t{4096}});
    const crucible::DagRef<crucible::RegionNode> region =
        crucible::mint_dag_node<crucible::RegionNode>(::foundation::effects::testing::test().alloc, arena);
    crucible::ReplayEngine engine{};
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture gives a raw pointer to a typed door, and the door refuses it")
    const crucible::PoolAllocator* pool = nullptr;
    engine.init(*region, pool);
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    return 0;
}
