// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-009: federation per-role mints now take a
// `SharedPermission<FederatedPeer<Org>>` admittance witness by value,
// produced by `SharedPermissionPool<...>::lend()->token()`.  The
// exclusive `Permission<FederatedPeer<Org>>` is move-only and is
// parked once into the pool at admission; production callers MUST
// route through the pool.
//
// Violation: pass a raw `Permission<FederatedPeer<Org>>` directly to
// `mint_sender`.  Permission and SharedPermission share the Tag but
// are distinct wrapper types — Permission is `Linear<>`-derived and
// move-only (sizeof = 1, no copy ctor), SharedPermission is a copyable
// empty proof token (sizeof = 1) that carries the pool refcount
// witness.  There is no implicit conversion between them; the call
// fails the parameter-type concept check.
//
// Pairs with neg_federation_mint_wrong_org_share.cpp (distinct
// rejection class: SharedPermission Org mismatch).  Both exercise the
// fractional-permission discipline introduced by fixy-A2-009 — closing
// the linearity-defeat gap left by fixy-CR-07's const-ref encoding.
//
// Expected diagnostic: no matching function | could not convert |
//                      mismatched parameter type | template argument
//                      deduction/substitution failed.

#include <crucible/permissions/FederationPermission.h>
#include <crucible/sessions/FederationProtocol.h>

#include <utility>

namespace fp   = ::crucible::safety::proto::federation;
namespace perm = ::crucible::permissions;
namespace saf  = ::crucible::safety;
namespace eff  = ::crucible::effects;

namespace neg_fed_raw_perm {
struct PeerOrg {};
struct TraceKey {};
struct Endpoint {};
}

// fixy-CR-13: federation mints require Row<IO, Block> in ctx::row_type.
using FederationFitCtx = decltype(
    eff::BgCompileCtx{}.in_row<eff::Row<
        eff::Effect::Bg, eff::Effect::Alloc,
        eff::Effect::IO, eff::Effect::Block>>());

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

int main() {
    auto local = saf::mint_permission_root<perm::tag::LocalCipherTag>();
    auto handshake =
        perm::make_self_signed_handshake<neg_fed_raw_perm::PeerOrg>(
            /*peer_key_fp=*/perm::PeerKeyFingerprint{0xFEDCDEULL},
            /*nonce=*/perm::Nonce{0xC0FFEEULL});
    auto admitted = perm::mint_federation_admittance<
        neg_fed_raw_perm::PeerOrg,
        perm::policy::admit_orgs<neg_fed_raw_perm::PeerOrg>>(
            local, handshake);

    // *admitted is the raw `Permission<FederatedPeer<PeerOrg>>` — the
    // exclusive authority token.  The mint signature now expects
    // `SharedPermission<FederatedPeer<PeerOrg>>` (the empty proof
    // token from a pool guard).  No implicit conversion exists.
    FederationFitCtx ctx{};
    auto sender = fp::mint_sender<
        neg_fed_raw_perm::PeerOrg, neg_fed_raw_perm::TraceKey>(
        ctx, neg_fed_raw_perm::Endpoint{}, *admitted);
    (void)sender;
    return 0;
}

#pragma GCC diagnostic pop
