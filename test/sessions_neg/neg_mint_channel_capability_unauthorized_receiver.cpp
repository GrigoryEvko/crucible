// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-076: Capability<E, S> payloads carry Row<E>.  A receiver running under
// HotFgCtx must not receive Capability<IO, Bg>, even when the sender's
// BgCompileCtx admits IO.

#include <crucible/effects/Capability.h>
#include <crucible/sessions/SessionMint.h>

namespace eff   = crucible::effects;
namespace proto = crucible::safety::proto;

struct Resource {};

int main() {
    using Proto = proto::Send<eff::Capability<eff::Effect::IO, eff::Bg>,
                              proto::End>;

    eff::BgCompileCtx sender;
    eff::HotFgCtx receiver;
    [[maybe_unused]] auto channel =
        proto::mint_channel<Proto>(sender, receiver, Resource{}, Resource{});
    return 0;
}
