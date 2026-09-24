// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A share token proves that the guard's share is outstanding.  A token
// minted from a guard that dies at the end of the statement would prove
// a share that is already released.  A const lvalue reference binds a
// temporary, so the const& token() alone admits the call, and only the
// deleted const&& twin refuses it.
//
// Expected diagnostic: the deleted const&& token(), with its reason.

#include <foundation/permissions/Permission.h>

#include <utility>

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace

int main() {
    namespace perm = ::foundation::permissions;
    perm::SharedPermissionPool pool{perm::mint_permission_root<Region>()};
    [[maybe_unused]] auto token = (*pool.lend()).token();
    return 0;
}
