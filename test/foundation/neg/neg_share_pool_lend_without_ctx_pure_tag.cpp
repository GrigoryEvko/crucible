// Negative-compile test. This file must fail to compile.
//
// A lend fails while the exclusive is out, and another thread deposits the
// exclusive at a time that the schedule decides.  So a lend takes a context
// that holds Nondet, or the bare tag of Nondet for a tag whose row is empty.
// A lend with neither does not compile.
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
