// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// An operator override comes from startup or from a test.  A background
// context that holds the token still does not own Init or Test, so the
// gate refuses it.

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
    authority = policy.operator_override(::fixy::BgDrainCtx{::foundation::effects::testing::bg()}, std::move(authority),
                                         cog, crucible::warden::QuarantineState::Permanent, 1, 1);
    ::foundation::permissions::permission_drop(std::move(authority));
    return 0;
}
