// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-CR-05 fixture: the FederationPermission.h defensive
// splits_into_pack partial specialization rejects accidental N-ary
// cross-org splits.
//
// Scenario: a caller (no malicious intent, no user-side
// specialization in scope) reaches mint_permission_split_n with a
// children pack mixing OrgA and OrgB-typed FederatedPeer tags.
//
// Substrate manifest in FederationPermission.h:
//
//   template <typename Org, typename... Children>
//   struct splits_into_pack<
//       tag::FederatedPeer<Org>, Children...>
//       : std::bool_constant<(std::is_same_v<
//             Children,
//             tag::FederatedPeer<Org>> && ...)> {};
//
// If ANY child fails the `Children == FederatedPeer<Org>` check,
// the value is false_type and mint_permission_split_n's
// static_assert fires.
//
// This is the N-ary companion of neg_fixy_federation_cross_org_split.cpp.
// Together they ship the HS14 ≥2 floor for fixy-CR-05.
//
// Expected diagnostic: GCC's static_assert with the
// splits_into_pack-specialized message or the unsatisfied
// requires-clause naming the trait.

#include <crucible/permissions/FederationPermission.h>

#include <utility>

// fixy-CR-06 follow-up: mint_permission_root<FederatedPeer<...>> is
// concept-deleted in V1 — seed the OrgA permission via the legitimate
// admittance channel instead.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

namespace perm = crucible::permissions;
namespace saf  = crucible::safety;

struct NegCrossOrgSplitN_OrgA {};
struct NegCrossOrgSplitN_OrgB {};

int main() {
    auto local_cipher =
        saf::mint_permission_root<perm::tag::LocalCipherTag>();
    auto handshake =
        perm::make_self_signed_handshake<NegCrossOrgSplitN_OrgA>(
            /*peer_key_fp=*/perm::PeerKeyFingerprint{0xC0FFEE'C0FFEEULL},
            /*nonce=*/      perm::Nonce{0xC1C1'C1C1'C1C1'C1C1ULL});
    auto admitted = perm::mint_federation_admittance<
        NegCrossOrgSplitN_OrgA,
        perm::policy::admit_orgs<NegCrossOrgSplitN_OrgA>>(
            local_cipher, handshake);
    auto perm_a = std::move(*admitted);

    // N-ary cross-org split: OrgA → OrgA × OrgB × OrgA.  Even one
    // mismatched child (the OrgB in the middle) makes the fold
    // false, mint_permission_split_n's static_assert fires.  Must
    // NOT compile.
    auto children = saf::mint_permission_split_n<
        perm::tag::FederatedPeer<NegCrossOrgSplitN_OrgA>,
        perm::tag::FederatedPeer<NegCrossOrgSplitN_OrgB>,
        perm::tag::FederatedPeer<NegCrossOrgSplitN_OrgA>>(
            std::move(perm_a));

    (void)children;
    return 0;
}

#pragma GCC diagnostic pop
