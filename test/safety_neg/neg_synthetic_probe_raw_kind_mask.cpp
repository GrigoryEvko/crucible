// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// Registered transports are typed as Bits<TransportProbeKind>.  A raw
// integer mask must not cross the API, because it could come from a
// different feature universe.

#include <crucible/observe/SyntheticProbe.h>

namespace cog = crucible::cog;
namespace observe = crucible::observe;

int main() {
    auto runner =
        observe::mint_synthetic_probes<::fixy::ColdInitCtx, 1>(::fixy::ColdInitCtx{::foundation::effects::testing::init()});
    cog::CogIdentity peer{};
    peer.uuid = cog::Uuid{0x141, 0x1};
    return runner.register_peer(peer, 1u) ? 0 : 1;
}
