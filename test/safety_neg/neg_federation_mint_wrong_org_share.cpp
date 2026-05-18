// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-009: federation per-role mints take a
// `SharedPermission<FederatedPeer<Org>>` admittance witness by value;
// the Org parameter is carried in the wrapper's tag.  Passing a
// `SharedPermission<FederatedPeer<OrgA>>` to `mint_sender<OrgB, ...>`
// is a hard type mismatch — closes the cross-org session
// impersonation gap that paralleled the cross-org permission-split
// gap closed in fixy-CR-05.
//
// Violation: admit OrgA via mint_federation_admittance + pool, lend a
// guard, take the SharedPermission<FederatedPeer<OrgA>> token, then
// pass it to mint_sender<OrgB, ...>.  The parameter type expected by
// mint_sender<OrgB, ...> is SharedPermission<FederatedPeer<OrgB>>.
// The mismatch is a parameter-type concept failure at the call site;
// no implicit conversion exists between SharedPermission<Tag1> and
// SharedPermission<Tag2> when Tag1 != Tag2.
//
// Pairs with neg_federation_mint_raw_permission.cpp (distinct
// rejection class: passing raw Permission instead of SharedPermission).
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

namespace neg_fed_wrong_org {
struct OrgA {};
struct OrgB {};
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
    auto handshake_a =
        perm::make_self_signed_handshake<neg_fed_wrong_org::OrgA>(
            /*peer_key_fp=*/perm::PeerKeyFingerprint{0xFEDCDEULL},
            /*nonce=*/perm::Nonce{0xC0FFEEULL});
    auto admitted_a = perm::mint_federation_admittance<
        neg_fed_wrong_org::OrgA,
        perm::policy::admit_orgs<neg_fed_wrong_org::OrgA>>(
            local, handshake_a);
    auto pool_a = fp::mint_federation_pool<neg_fed_wrong_org::OrgA>(
        std::move(*admitted_a));
    auto guard_a = pool_a.lend();

    // guard_a->token() is SharedPermission<FederatedPeer<OrgA>>.
    // mint_sender<OrgB, ...> expects SharedPermission<FederatedPeer<OrgB>>.
    // The tag mismatch is structural and cannot be silently coerced.
    FederationFitCtx ctx{};
    auto sender = fp::mint_sender<
        neg_fed_wrong_org::OrgB, neg_fed_wrong_org::TraceKey>(
        ctx, neg_fed_wrong_org::Endpoint{}, guard_a->token());
    (void)sender;
    return 0;
}

#pragma GCC diagnostic pop
