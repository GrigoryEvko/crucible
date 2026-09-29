// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A health fact changes the quarantine state only from a context that
// owns the Bg atom.  The hot foreground context has an empty row.

#include <crucible/warden/Quarantine.h>
#include <fixy/Ctx.h>

int main() {
    auto policy =
        crucible::warden::mint_quarantine_policy<2>(::fixy::ColdInitCtx{::foundation::effects::testing::init()});
    crucible::cog::CogIdentity cog{};
    cog.uuid = crucible::cog::Uuid{0x118, 0x1};
    crucible::topology::HealthSnapshot health{};
    ::fixy::HotFgCtx const foreground = ::foundation::effects::testing::foreground();
    (void)policy.on_health_event(foreground, cog, health, 1);
    return 0;
}
