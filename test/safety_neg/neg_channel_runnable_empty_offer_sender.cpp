// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-CR-15: `mint_channel<Offer<Sender<Role>>>` is rejected by
// CtxFitsChannel — exercises the Sender-annotated empty-Offer
// specialization of protocol_permissioned_runnable.  Before
// CR-15, the variadic AND-fold over the empty real-branches pack
// (Sender<Role> is the annotation, not a branch) collapsed to
// true.
//
// Pairs with the bare Select<> / Offer<> fixtures: this is the
// third distinct rejection shape, covering the MPST-annotated
// Offer form added in #367.
//
// Expected diagnostic: ProtocolPermissionedRunnable /
// CtxFitsChannel / constraints not satisfied.

#include <crucible/sessions/SessionMint.h>
#include <crucible/effects/ExecCtx.h>

namespace proto = ::crucible::safety::proto;
namespace eff   = ::crucible::effects;

struct PinnedResource : ::crucible::safety::Pinned<PinnedResource> {
    int sentinel = 3;
};

struct AliceRole {};

void compile_time_reject() {
    eff::HotFgCtx ctx{};
    PinnedResource ra, rb;
    using EmptyAnnotated = proto::Offer<proto::Sender<AliceRole>>;
    auto channel = proto::mint_channel<EmptyAnnotated>(
        ctx, ctx, ra, rb);
    (void)channel;
}

int main() { return 0; }
