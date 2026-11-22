// Negative-compile test.  This file must fail to compile.
//
// An operator override forces a state only with the OperatorOverride
// permission token.  A call with no token has no matching overload.

#include <crucible/warden/Quarantine.h>
#include <fixy/Ctx.h>

int main() {
    ::fixy::ColdInitCtx const startup{::foundation::effects::testing::init()};
    auto policy = crucible::warden::mint_quarantine_policy<2>(startup);
    crucible::cog::CogIdentity cog{};
    cog.uuid = crucible::cog::Uuid{0x118, 0x1};
    (void)policy.operator_override(startup, cog, crucible::warden::QuarantineState::Permanent, 1, 1);
    return 0;
}
