// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-076: the channel walker must inspect the whole local protocol, not only
// its first action.  Endpoint A first sends a pure int, then later receives an
// IO payload; HotFgCtx cannot admit that continuation Recv row.

#include <crucible/effects/Computation.h>
#include <crucible/sessions/SessionMint.h>

namespace eff   = crucible::effects;
namespace proto = crucible::safety::proto;

struct Payload {};
struct Resource {};

int main() {
    using IoPayload = eff::Computation<eff::Row<eff::Effect::IO>, Payload>;
    using Proto = proto::Send<int, proto::Recv<IoPayload, proto::End>>;

    eff::HotFgCtx endpoint_a;
    eff::BgCompileCtx endpoint_b;
    [[maybe_unused]] auto channel =
        proto::mint_channel<Proto>(endpoint_a, endpoint_b,
                                   Resource{}, Resource{});
    return 0;
}
