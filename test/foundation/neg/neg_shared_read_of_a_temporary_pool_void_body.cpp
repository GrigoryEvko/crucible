// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The void form of with_shared_read makes the same shape check as the
// value form.  A pool passed as an rvalue is one the caller is giving
// up, so the shape check refuses it here too.  The body here returns
// nothing.
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
    [[maybe_unused]] bool ran = perm::with_shared_read(std::move(pool), [](auto) noexcept {});
    return 0;
}
