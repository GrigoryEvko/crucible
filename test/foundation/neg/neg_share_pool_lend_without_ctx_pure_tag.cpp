// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A lend fails while the exclusive is out, and another thread deposits the
// exclusive at a time that the schedule decides.  So a lend takes a context
// that holds Nondet, also for a tag whose row is empty.  The lend with no
// context stays only as the internal door of the channels of the base.
//
// Expected diagnostic: lend has no candidate with no argument.

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
    [[maybe_unused]] auto share = pool.lend();
    return 0;
}
