// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-076: when SessionMint.h is visible, legacy
// mint_channel<Proto>(resource_a, resource_b) is a HotFg/HotFg compatibility
// shim for permissioned-runnable protocols.  A protocol that carries IO cannot
// silently fall back to structural channel minting.

#include <crucible/effects/Computation.h>
#include <crucible/sessions/SessionMint.h>

namespace eff   = crucible::effects;
namespace proto = crucible::safety::proto;

struct Payload {};
struct Resource {};

int main() {
    using IoPayload = eff::Computation<eff::Row<eff::Effect::IO>, Payload>;
    using Proto = proto::Send<IoPayload, proto::End>;

    [[maybe_unused]] auto channel =
        proto::mint_channel<Proto>(Resource{}, Resource{});
    return 0;
}
