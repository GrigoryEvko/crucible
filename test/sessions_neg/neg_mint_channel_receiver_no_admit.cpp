// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-076: paired mint_channel must reject a receiver context that does not
// admit the payload row sent by the peer.  Endpoint A can send IO under
// BgCompileCtx; endpoint B would receive that IO payload under HotFgCtx, which
// has Row<> and must be rejected by CtxFitsChannel.

#include <crucible/effects/Computation.h>
#include <crucible/sessions/SessionMint.h>

namespace eff   = crucible::effects;
namespace proto = crucible::safety::proto;

struct Payload {};
struct Resource {};

int main() {
    using IoPayload = eff::Computation<eff::Row<eff::Effect::IO>, Payload>;
    using Proto = proto::Send<IoPayload, proto::End>;

    eff::BgCompileCtx sender;
    eff::HotFgCtx receiver;
    [[maybe_unused]] auto channel =
        proto::mint_channel<Proto>(sender, receiver, Resource{}, Resource{});
    return 0;
}
