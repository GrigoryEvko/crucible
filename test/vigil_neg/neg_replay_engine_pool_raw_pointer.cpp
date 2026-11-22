// Negative-compile test.  This file must fail to compile.
//
// ReplayEngine::init takes the pool as a fixy::Ref of a PoolAllocator.  A
// raw PoolAllocator* must not cross the init boundary.
//
// Expected diagnostic: no init overload accepting PoolAllocator*.

#include <crucible/ReplayEngine.h>
#include <foundation/Quarantine.h>

int main() {
    crucible::ReplayEngine engine{};
    const crucible::RegionNode region{};
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture gives a raw pointer to a typed door, and the door refuses it")
    const crucible::PoolAllocator* pool = nullptr;
    engine.init(region, pool);
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    return 0;
}
