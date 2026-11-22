// Negative-compile test.  This file must fail to compile.
//
// ReplayEngine::init requires a Ref of the PoolAllocator.  A fixy::Ref of
// another owner type cannot substitute, even though each Ref has the size of
// a pointer.
//
// Expected diagnostic: no init overload accepting a Ref of a RegionNode.

#include <crucible/ReplayEngine.h>

int main() {
    crucible::ReplayEngine engine{};
    crucible::RegionNode region{};
    engine.init(region, ::fixy::to(region));
    return 0;
}
