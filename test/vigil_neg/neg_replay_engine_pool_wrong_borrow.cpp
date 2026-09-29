// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// ReplayEngine::init requires a borrow of the PoolAllocator.  A
// fixy::BorrowedRef over another owner type cannot
// substitute, even though all BorrowedRef instantiations are
// pointer-sized.
//
// Expected diagnostic: no init overload accepting BorrowedRef<RegionNode>.

#include <crucible/ReplayEngine.h>

int main() {
    crucible::ReplayEngine engine{};
    crucible::RegionNode region{};
    engine.init(&region, ::fixy::BorrowedRef<crucible::RegionNode>{region});
    return 0;
}
