// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// The foreground hot-path context cannot change heartbeat, thermal,
// ECC, drop or wear state.  A foreground context is built only from the
// producer claim, so the refused call names it in a decltype operand.

#include <crucible/topology/Health.h>

#include <utility>

int main() {
    auto scorer = crucible::topology::mint_topology_health<::fixy::ColdInitCtx, 2>(
        ::fixy::ColdInitCtx{::foundation::effects::testing::init()});
    crucible::cog::CogIdentity peer{};
    peer.uuid = crucible::cog::Uuid{0x113, 0x5};

    using refused = decltype(scorer.update_thermal(std::declval<::fixy::HotFgCtx const&>(), peer,
                                                   crucible::topology::ThermalSample{}));
    return sizeof(refused) == 0 ? 1 : 0;
}
