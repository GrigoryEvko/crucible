// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-CR-15: `mint_channel<Offer<>>` is rejected on
// CtxFitsChannel — exercises the Offer<> specialization of
// protocol_permissioned_runnable.  Distinct from the Select<>
// fixture: Select<> is INTERNAL choice (this endpoint picks),
// Offer<> is EXTERNAL choice (peer signals); both have empty
// branch packs after CR-15 explicitly false_type'd.
//
// Note: dual(Offer<>) = Select<>, so this fixture's rejection
// fires on the local-side runnable check OR the dual-side check —
// either is acceptable; the CR-15 fix covers both.
//
// Expected diagnostic: ProtocolPermissionedRunnable /
// CtxFitsChannel / constraints not satisfied.

#include <crucible/sessions/SessionMint.h>
#include <crucible/effects/ExecCtx.h>

namespace proto = ::crucible::safety::proto;
namespace eff   = ::crucible::effects;

struct PinnedResource : ::crucible::safety::Pinned<PinnedResource> {
    int sentinel = 2;
};

void compile_time_reject() {
    eff::HotFgCtx ctx{};
    PinnedResource ra, rb;
    auto channel = proto::mint_channel<proto::Offer<>>(
        ctx, ctx, ra, rb);
    (void)channel;
}

int main() { return 0; }
