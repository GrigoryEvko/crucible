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

namespace perm = crucible::permissions;
namespace saf  = crucible::safety;

struct NegCrossOrgSplitN_OrgA {};
struct NegCrossOrgSplitN_OrgB {};

int main() {
    auto perm_a = saf::mint_permission_root<
        perm::tag::FederatedPeer<NegCrossOrgSplitN_OrgA>>();

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
