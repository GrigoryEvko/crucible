// fixy_neg: mint_federation_channel rejects when the underlying
// federation protocol cannot bind to the given endpoint pair.
//
// HS14 floor for fixy::sess::mint_federation_channel (fixy/Sess.h
// §B6 forwarder).  mint_federation_channel forwards into
// federation::mint_channel, which calls mint_permissioned_session for
// each endpoint.  mint_permissioned_session is constrained
// `CtxFitsPermissionedProtocol<SenderProto<KeyTag>, Ctx, PermSet<>>`.
// Passing a `void*` as the sender endpoint cannot satisfy the
// protocol's resource-shape concept gate, so the constraint chain
// fires at the inner mint_permissioned_session call.
//
// Distinct from the non-ctx sibling fixture: this exercises the
// downstream-template concept failure rather than the surface-level
// IsExecCtx<Ctx> gate.
//
// Expected diagnostic: "CtxFitsPermissionedProtocol" — constraint
// satisfaction failure at the mint_permissioned_session level.

#include <crucible/fixy/Sess.h>

namespace fsess = crucible::fixy::sess;

struct FakeKey {};

int main() {
    // Use the canonical BgCompileCtx so IsExecCtx<Ctx> is satisfied,
    // forcing the constraint chain to reach the inner
    // mint_permissioned_session call and fail there.
    crucible::effects::BgCompileCtx ctx{};
    void* bad_sender = nullptr;
    void* bad_receiver = nullptr;
    auto bad = fsess::mint_federation_channel<FakeKey>(
        ctx, bad_sender, bad_receiver);
    (void)bad;
    return 0;
}
