// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-CR-05 fixture: the FederationPermission.h defensive
// splits_into partial specialization rejects accidental cross-org
// splits.
//
// Scenario: a caller (no malicious intent, no user-side
// specialization in scope) reaches mint_permission_split with
// cross-org template arguments — Permission<FederatedPeer<OrgA>>
// → Permission<FederatedPeer<OrgB>> × Permission<FederatedPeer<OrgB>>.
//
// Substrate manifest in FederationPermission.h:
//
//   template <typename Org, typename A, typename B>
//   struct splits_into<
//       tag::FederatedPeer<Org>,
//       tag::FederatedPeer<A>,
//       tag::FederatedPeer<B>>
//       : std::bool_constant<std::is_same_v<A, Org>
//                            && std::is_same_v<B, Org>> {};
//
// For A != Org OR B != Org, the value is false_type.
// mint_permission_split has `static_assert(splits_into_v<In, L, R>,
// "...");` which fires.
//
// Distinct from the positive-attack regression at
// test/safety_attack/attack_federation_cross_org_escalation.cpp:
//   - The attack file SHIPS a malicious user-side full specialization
//     in scope, which overrides our partial (wins partial-spec
//     ranking via three concrete types) — the attack compiles today.
//   - THIS fixture is the inverse: with NO malicious specialization
//     in scope, the defensive partial correctly rejects, and the
//     mint_permission_split static_assert fires.  Proves the
//     defense closes accidental cross-org splits even when the
//     trait gap (fixy-M-29) is not yet closed.
//
// Expected diagnostic: GCC's static_assert with the
// splits_into-specialized message ("splits_into<In, L, R>::value to
// be specialized true") or the unsatisfied requires-clause naming
// the trait.

#include <crucible/permissions/FederationPermission.h>

#include <utility>

// fixy-CR-06 follow-up: mint_permission_root<FederatedPeer<...>> is
// concept-deleted in V1 — seed the OrgA permission via the legitimate
// admittance channel instead.  Self-signed handshakes are
// [[deprecated]] in V1 (fixy-CR-02); suppress the diagnostic at the
// TU level so the splits_into static_assert remains the failing site.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

namespace perm = crucible::permissions;
namespace saf  = crucible::safety;

// Disjoint orgs declared in TU scope so explicit splits_into
// specializations naming them remain possible — but NONE is shipped
// by this fixture.  The substrate-only path must therefore fail.
struct NegCrossOrgSplit_OrgA {};
struct NegCrossOrgSplit_OrgB {};

int main() {
    auto local_cipher =
        saf::mint_permission_root<perm::tag::LocalCipherTag>();
    auto handshake =
        perm::make_self_signed_handshake<NegCrossOrgSplit_OrgA>(
            /*peer_key_fp=*/0xC0FFEE'C0FFEEULL,
            /*nonce=*/      0xC1C1'C1C1'C1C1'C1C1ULL);
    auto admitted = perm::mint_federation_admittance<
        NegCrossOrgSplit_OrgA,
        perm::policy::admit_orgs<NegCrossOrgSplit_OrgA>>(
            local_cipher, handshake);
    auto perm_a = std::move(*admitted);

    // Cross-org split: OrgA → OrgB × OrgB.  Must NOT compile —
    // FederationPermission.h's defensive partial sets splits_into_v
    // to false for any (Org, A, B) with A != Org or B != Org.
    auto [b1, b2] = saf::mint_permission_split<
        perm::tag::FederatedPeer<NegCrossOrgSplit_OrgB>,
        perm::tag::FederatedPeer<NegCrossOrgSplit_OrgB>>(
            std::move(perm_a));

    (void)b1;
    (void)b2;
    return 0;
}

#pragma GCC diagnostic pop
