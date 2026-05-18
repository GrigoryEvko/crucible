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
// fixy-CR-07 + fixy-A2-009: federation mints now also take a
// `SharedPermission<FederatedPeer<Org>>` admittance witness (by value);
// the exclusive `Permission` parks in a `SharedPermissionPool` once
// and per-call sites pass `pool.lend()->token()`.  We bootstrap a
// legitimate admittance + pool so the call is syntactically
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

#include <utility>

namespace fsess = crucible::fixy::sess;
namespace cs    = crucible::safety;
namespace ff    = crucible::fixy::source::federation;
namespace eff   = crucible::effects;

// fixy-CR-13: federation mints now also require Row<IO, Block> in
// ctx::row_type.  This fixture targets the *downstream*
// SessionResource_NotPinned check, so the surface row gate must pass
// — widen BgCompileCtx to admit Block via `.in_row<>()`.
using FederationFitCtx = decltype(
    eff::BgCompileCtx{}.in_row<eff::Row<
        eff::Effect::Bg, eff::Effect::Alloc,
        eff::Effect::IO, eff::Effect::Block>>());

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
    auto pool = fsess::federation::mint_federation_pool<
        NegFedChannelWrongEp_PeerOrg>(std::move(*admitted));
    auto guard = pool.lend();

    // fixy-CR-13: use the widened FederationFitCtx so the surface row
    // gate (CtxFitsFederation) is satisfied; the constraint chain then
    // reaches the inner mint_permissioned_session call and fires
    // SessionResource_NotPinned for the void* endpoints.
    FederationFitCtx ctx{};
    void* bad_sender = nullptr;
    void* bad_receiver = nullptr;
    auto bad = fsess::mint_federation_channel<
        NegFedChannelWrongEp_PeerOrg>(
        ctx, bad_sender, bad_receiver, guard->token());
    (void)bad;
    return 0;
}

#pragma GCC diagnostic pop
