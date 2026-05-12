// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 for GAPS-141. Registered transports are typed as
// Bits<TransportProbeKind>; raw integer masks must not cross the API
// because they could come from a different feature universe.

#include <crucible/observe/SyntheticProbe.h>

namespace cog = crucible::cog;
namespace eff = crucible::effects;
namespace observe = crucible::observe;

int main() {
    auto runner = observe::mint_synthetic_probes<eff::ColdInitCtx, 1>(
        eff::ColdInitCtx{});
    cog::CogIdentity peer{};
    peer.uuid = cog::Uuid{0x141, 0x1};
    return runner.register_peer(peer, 1u) ? 0 : 1;
}
