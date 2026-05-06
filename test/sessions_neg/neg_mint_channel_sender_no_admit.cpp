// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-076: paired mint_channel must reject a sender context that does not
// admit the row carried by a payload it sends.  BgDrainCtx admits Bg/Alloc but
// not IO; the receiver is deliberately wide enough so the failure is isolated
// to endpoint A.

#include <crucible/effects/Computation.h>
#include <crucible/sessions/SessionMint.h>

namespace eff   = crucible::effects;
namespace proto = crucible::safety::proto;

struct Payload {};
struct Resource {};

int main() {
    using IoPayload = eff::Computation<eff::Row<eff::Effect::IO>, Payload>;
    using Proto = proto::Send<IoPayload, proto::End>;

    eff::BgDrainCtx sender;
    eff::BgCompileCtx receiver;
    [[maybe_unused]] auto channel =
        proto::mint_channel<Proto>(sender, receiver, Resource{}, Resource{});
    return 0;
}
