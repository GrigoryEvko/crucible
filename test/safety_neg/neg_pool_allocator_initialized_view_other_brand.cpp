// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The live view of the pool is minted only from the foreground context of
// a Vigil's producer claim.  The context of another state's claim proves
// that its holder owns that state, not a Vigil.

#include <crucible/PoolAllocator.h>

namespace {
struct Stranger {};
}  // namespace

int main() {
    crucible::PoolAllocator pool;
    auto pv = pool.mint_initialized_view(::foundation::effects::testing::foreground<Stranger>());
    (void)pv;
    return 0;
}
