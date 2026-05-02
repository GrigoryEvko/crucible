// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// AUDIT-3 (CLAUDE.md §XXI HS14): mint_substrate_session<Substr, Dir>
// (ctx, h) rejects (Substrate, Direction) pairs that aren't bridgeable.
// SPSC + SwmrWriter has no default_proto_for / handle_for spec.
//
// Violation: PermissionedSpscChannel + Direction::SwmrWriter →
// IsBridgeableDirection fails.

#include <crucible/concurrent/SubstrateSessionBridge.h>

namespace eff  = crucible::effects;
namespace conc = crucible::concurrent;

struct UserTag {};

int main() {
    using Channel = conc::PermissionedSpscChannel<int, 64, UserTag>;

    typename Channel::ProducerHandle* fake_handle = nullptr;

    eff::HotFgCtx fg;
    // SPSC + SwmrWriter → IsBridgeableDirection fails.
    auto bad = conc::mint_substrate_session<Channel, conc::Direction::SwmrWriter>(
        fg, *fake_handle);
    (void)bad;
    return 0;
}
