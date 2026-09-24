// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A root mint without a context names no scope that may touch the
// region.  That is sound only for a pure tag.  A tag whose row names IO
// must be minted under a context that admits IO, so the token form of
// the root mint refuses it.  The fit concept admits the call, because
// with no context it has no row to compare, and the static_assert in the
// body is the only check.
//
// Expected diagnostic: the static_assert in mint_permission_root that
// asks for an empty row when no context is passed.

#include <foundation/permissions/Permission.h>

int main() {
    [[maybe_unused]] auto token =
        ::foundation::permissions::mint_permission_root<::foundation::permissions::tag::HugePageTag>();
    return 0;
}
