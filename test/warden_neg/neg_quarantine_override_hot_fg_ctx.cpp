// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// An operator override comes from startup or from a test.  The hot
// foreground context has an empty row, so the gate refuses it.

#include <crucible/warden/Quarantine.h>
#include <fixy/Ctx.h>
#include <foundation/permissions/Permission.h>

int main() {
    ::fixy::ColdInitCtx const startup{::foundation::effects::testing::init()};
    auto policy = crucible::warden::mint_quarantine_policy<2>(startup);
    crucible::cog::CogIdentity cog{};
    cog.uuid = crucible::cog::Uuid{0x118, 0x1};
    auto authority =
        ::foundation::permissions::mint_permission_root<crucible::warden::quarantine_tag::OperatorOverride>(startup);
    ::fixy::HotFgCtx const foreground = ::foundation::effects::testing::foreground();
    authority = policy.operator_override(foreground, std::move(authority), cog,
                                         crucible::warden::QuarantineState::Permanent, 1, 1);
    ::foundation::permissions::permission_drop(std::move(authority));
    return 0;
}
