// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A root of the operator override tag is the authority to force a
// quarantine state.  A background drain context does not own Init, so
// it does not admit the row of the tag, and the root mint refuses it.
//
// Expected diagnostic: no root mint of OperatorOverride matches a
// background drain context, because PermissionRootArgs is not satisfied.

#include <crucible/warden/Quarantine.h>
#include <fixy/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

int main() {
    ::fixy::BgDrainCtx const drain{::foundation::effects::testing::bg()};
    auto authority =
        ::foundation::permissions::mint_permission_root<crucible::warden::quarantine_tag::OperatorOverride>(drain);
    ::foundation::permissions::permission_drop(std::move(authority));
    return 0;
}
