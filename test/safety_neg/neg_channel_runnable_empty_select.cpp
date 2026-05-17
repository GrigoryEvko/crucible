// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-CR-15: `mint_channel<Select<>>` is rejected by
// CtxFitsChannel's `ProtocolPermissionedRunnable<Proto>` clause.
// Before CR-15, the trait's variadic AND-fold over `Branches...`
// collapsed to `true` on the empty branch pack — the channel mint
// would admit an unrunnable protocol whose first action is to
// pick from a dead Select<>.
//
// Pairs with neg_channel_runnable_empty_offer.cpp (Offer<> form,
// hits the dual side of CtxFitsChannel) and
// neg_channel_runnable_empty_offer_sender.cpp (Offer<Sender<R>>
// form, hits the Sender-annotated specialization).
//
// Expected diagnostic: ProtocolPermissionedRunnable /
// CtxFitsChannel / constraints not satisfied.

#include <crucible/sessions/SessionMint.h>
#include <crucible/effects/ExecCtx.h>

namespace proto = ::crucible::safety::proto;
namespace eff   = ::crucible::effects;

struct PinnedResource : ::crucible::safety::Pinned<PinnedResource> {
    int sentinel = 1;
};

void compile_time_reject() {
    eff::HotFgCtx ctx{};
    PinnedResource ra, rb;
    auto channel = proto::mint_channel<proto::Select<>>(
        ctx, ctx, ra, rb);
    (void)channel;
}

int main() { return 0; }
