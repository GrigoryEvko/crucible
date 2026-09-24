// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FairSharedPermissionPool's BurstLimit must be > 0.  BurstLimit=0
// would mean the writer's gated try_upgrade() can NEVER succeed
// (would always observe wins >= 0 == BurstLimit).  Indefinite
// writer starvation.  Static-asserted at the wrapper.
//
// Expected diagnostic: the static_assert message
//   "FairSharedPermissionPool BurstLimit must be > 0"

#include <crucible/permissions/FairSharedPermissionPool.h>

namespace saf = crucible::safety;

struct ZeroLimit {};

int main() {
    auto exc = saf::mint_permission_root<ZeroLimit>();
    saf::FairSharedPermissionPool<ZeroLimit, 0> pool{std::move(exc)};
    return 0;
}
