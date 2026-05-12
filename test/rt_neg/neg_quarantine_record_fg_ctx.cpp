#include <crucible/rt/Quarantine.h>

int main() {
    auto policy = crucible::rt::mint_quarantine_policy<
        crucible::effects::ColdInitCtx, 2>(crucible::effects::ColdInitCtx{});
    crucible::cog::CogIdentity cog{};
    cog.uuid = crucible::cog::Uuid{0x118, 0x1};
    crucible::topology::HealthSnapshot health{};
    (void)policy.on_health_event(crucible::effects::HotFgCtx{}, cog, health, 1);
    return 0;
}
