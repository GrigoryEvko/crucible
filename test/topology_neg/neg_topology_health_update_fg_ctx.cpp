#include <crucible/topology/Health.h>

int main() {
    auto scorer = crucible::topology::mint_topology_health<
        crucible::effects::ColdInitCtx, 2>(
            crucible::effects::ColdInitCtx{});
    crucible::cog::CogIdentity peer{};
    peer.uuid = crucible::cog::Uuid{0x113, 0x5};

    (void)scorer.update_thermal(
        crucible::effects::HotFgCtx{},
        peer,
        crucible::topology::ThermalSample{});
    return 0;
}
