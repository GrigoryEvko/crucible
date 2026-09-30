// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A root of the operator override tag is the authority to force a
// quarantine state.  The row of the tag names Init, so the root mint
// with no context refuses the tag.
//
// Expected diagnostic: the static assertion of mint_permission_root that
// refuses a context-free mint of a tag whose row is not empty, for the
// tag OperatorOverride.

#include <crucible/warden/Quarantine.h>
#include <foundation/permissions/Permission.h>

#include <utility>

int main() {
    auto authority =
        ::foundation::permissions::mint_permission_root<crucible::warden::quarantine_tag::OperatorOverride>();
    ::foundation::permissions::permission_drop(std::move(authority));
    return 0;
}
