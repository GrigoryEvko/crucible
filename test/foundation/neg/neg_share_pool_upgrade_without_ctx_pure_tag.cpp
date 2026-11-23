// Negative-compile test. This file must fail to compile.
//
// An upgrade fails while a share is out, and another thread ends a share at
// a time that the schedule decides.  So an upgrade takes a context that
// holds Nondet, or the bare tag of Nondet for a tag whose row is empty.  An
// upgrade with neither does not compile.
//
// Expected diagnostic: try_upgrade has no candidate with no argument.

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/SharedPermission.h>

namespace {
struct Pure {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace

int main() {
    namespace perm = ::foundation::permissions;
    perm::SharedPermissionPool pool{perm::mint_permission_root<Pure>()};
    [[maybe_unused]] auto exclusive = pool.try_upgrade();
    return 0;
}
