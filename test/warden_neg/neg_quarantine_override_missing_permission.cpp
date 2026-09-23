#include <crucible/warden/Quarantine.h>

int main() {
    auto policy =
        crucible::warden::mint_quarantine_policy<crucible::effects::ColdInitCtx, 2>(crucible::effects::ColdInitCtx{::crucible::effects::testing::init()});
    crucible::cog::CogIdentity cog{};
    cog.uuid = crucible::cog::Uuid{0x118, 0x1};
    (void)policy.operator_override(crucible::effects::ColdInitCtx{::crucible::effects::testing::init()}, cog, crucible::warden::QuarantineState::Permanent,
                                   1, 1);
    return 0;
}
