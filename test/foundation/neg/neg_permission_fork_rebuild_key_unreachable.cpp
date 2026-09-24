// The rebuild key is the whole gate on reissuing a parent permission
// after a structured join, so a translation unit outside the friend
// list must not be able to build one.
//
// ForkRebuildAccess::rebuild<T> is a public static on a public struct
// and carries no constraint on T.  It mints a Permission for whatever
// tag it is given.  The only thing standing between a caller and a
// Permission for a tag it does not own is the private default
// constructor of ForkRebuildKey, whose sole friend is
// PermissionForkRunner.  Its one builder of the key is the fork body,
// which takes the parent Permission by rvalue, consumes it at the split,
// and is reachable from the two fork mints alone.
//
// This fixture is the witness that the access check is real.  It is the
// companion of neg_permission_fork_rebuild_no_nullary_door.cpp: that one
// proves the historical bypass is gone by name, this one proves the gate
// itself holds against a caller who reaches for the key directly.
//
// VIOLATION: a user TU builds the key and mints a permission for a tag
// it never owned.
//
// Expected diagnostic: the key's constructor is private in this context.

#include <foundation/permissions/Permission.h>

namespace {
struct StrangerTag {};
}  // namespace

int main() {
    namespace detail = ::foundation::permissions::detail;
    auto forged = detail::ForkRebuildAccess::rebuild<StrangerTag>(detail::ForkRebuildKey{});
    (void)forged;
    return 0;
}
