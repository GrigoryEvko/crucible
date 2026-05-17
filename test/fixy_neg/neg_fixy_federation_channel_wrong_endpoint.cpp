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
// fixy-CR-07: federation mints now also take a
// `Permission<FederatedPeer<Org>> const&` admittance witness; we
// bootstrap a legitimate admittance so the call is syntactically
// well-formed and the constraint chain reaches the downstream
// CtxFitsPermissionedProtocol failure.
//
// Distinct from the non-ctx sibling fixture: this exercises the
// downstream-template concept failure rather than the surface-level
// IsExecCtx<Ctx> gate.
//
// Expected diagnostic: "CtxFitsPermissionedProtocol" — constraint
// satisfaction failure at the mint_permissioned_session level.

#include <crucible/fixy/Sess.h>
#include <crucible/fixy/Source.h>

namespace fsess = crucible::fixy::sess;
namespace cs    = crucible::safety;
namespace ff    = crucible::fixy::source::federation;

struct NegFedChannelWrongEp_PeerOrg {};

// CR-02/CR-03/CR-04 — mint_federation_admittance is [[deprecated]];
// suppress so the diagnostic does not interleave with the expected
// CtxFitsPermissionedProtocol regex.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

int main() {
    auto local = cs::mint_permission_root<ff::LocalCipherTag>();
    auto handshake = ff::make_self_signed_handshake<
        NegFedChannelWrongEp_PeerOrg>();
    auto admitted = ff::mint_federation_admittance<
        NegFedChannelWrongEp_PeerOrg>(local, handshake);

    // Use the canonical BgCompileCtx so IsExecCtx<Ctx> is satisfied,
    // forcing the constraint chain to reach the inner
    // mint_permissioned_session call and fail there.
    crucible::effects::BgCompileCtx ctx{};
    void* bad_sender = nullptr;
    void* bad_receiver = nullptr;
    auto bad = fsess::mint_federation_channel<
        NegFedChannelWrongEp_PeerOrg>(
        ctx, bad_sender, bad_receiver, *admitted);
    (void)bad;
    return 0;
}

#pragma GCC diagnostic pop
