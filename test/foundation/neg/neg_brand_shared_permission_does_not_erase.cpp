// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A share carries the brand of the exclusive it came from.  A callee that
// takes the erased spelling takes a share of any region of the tag, so a
// share of one region can prove a read of another.  No conversion drops
// the brand of a share, so the call does not compile.
//
// Expected diagnostic: no conversion from the branded share to the erased
// share, at the call of the function that takes the erased share.

#include <foundation/permissions/Permission.h>

#include <utility>

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

constexpr void takes_any_share(::foundation::permissions::SharedPermission<Region>) noexcept {}
}  // namespace

int main() {
    auto branded =
        ::foundation::permissions::mint_permission_share(::foundation::permissions::mint_permission_root<Region>());
    takes_any_share(branded);
    return 0;
}
