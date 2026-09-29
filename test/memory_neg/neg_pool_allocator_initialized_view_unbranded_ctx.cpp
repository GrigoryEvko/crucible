// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The live view of the pool is minted only from the foreground context of
// a Vigil's producer claim.  A context that names no claim states only
// that some thread holds some claim, so the gate refuses it.

#include <crucible/PoolAllocator.h>

int main() {
    crucible::PoolAllocator pool;
    auto pv = pool.mint_initialized_view(::foundation::effects::testing::foreground());
    (void)pv;
    return 0;
}
