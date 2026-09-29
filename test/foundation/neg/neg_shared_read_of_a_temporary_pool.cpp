// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// with_shared_read lends from the pool it is given, and the guard of that
// lend points into the pool.  A pool passed as an rvalue is one the
// caller is giving up, so the shape check of the door refuses it.  The
// body here returns a value.
//
// Expected diagnostic: with_shared_read has no viable candidate, and the
// note names WithSharedReadArgs.

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
    [[maybe_unused]] auto result = perm::with_shared_read(std::move(pool), [](auto) noexcept { return 7; });
    return 0;
}
