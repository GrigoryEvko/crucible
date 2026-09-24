// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FairSharedPermissionPool inherits from Pinned<...>: copy ctor +
// move ctor are deleted.  The wrapper's identity IS the address of
// its alignas(64) atomic counter; copying would create a duplicate
// counter desynchronized from the inner pool's state.
//
// Expected diagnostic: "use of deleted function" or
// "FairSharedPermissionPool" + "deleted".

#include <crucible/permissions/FairSharedPermissionPool.h>

namespace saf = crucible::safety;

struct CopyAttempt {};

int main() {
    auto exc1 = saf::mint_permission_root<CopyAttempt>();
    saf::FairSharedPermissionPool<CopyAttempt, 8> a{std::move(exc1)};

    // ATTEMPTED COPY-CONSTRUCT — must be a compile error.
    saf::FairSharedPermissionPool<CopyAttempt, 8> b{a};
    (void)b;
    return 0;
}
