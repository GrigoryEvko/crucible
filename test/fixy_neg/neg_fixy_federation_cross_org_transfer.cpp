// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-AUDIT-G8 fixture #3: cross-org Permission transfer is a hard
// compile-time error.
//
// Scenario: Org A holds a `FederatedPeerPermission<OrgA>` minted via
// the federation admittance flow.  An attacker (or a buggy caller)
// tries to bind that A-tagged permission to a function expecting a
// `FederatedPeerPermission<OrgB>`.  Permission<tag::FederatedPeer<OrgA>>
// and Permission<tag::FederatedPeer<OrgB>> are DISTINCT template
// instantiations with no implicit conversion, so the parameter cannot
// bind.
//
// This is the type-system-level "every org has its own permission
// universe" guarantee.  Without it, a compromised peer-A permission
// could trivially impersonate peer B at any boundary that consumes a
// FederatedPeerPermission<Org> — collapsing the cross-org isolation
// boundary that federation_org_id<Org> + admit_orgs<...> were
// designed to enforce.
//
// Distinct from fixtures #1-#2:
//   #1 wrong_org       — caller passes a FederatedPeerPermission instead
//                         of a LocalCipherPermission at admittance mint
//                         time.  Tag-mismatch at the mint call site.
//   #2 missing_proof   — caller passes a plain int as the proof.  No
//                         Permission token at all at the mint call site.
//   #3 cross_org_transfer — caller HAS a valid FederatedPeerPermission
//                         but for the WRONG Org.  Tag-mismatch at the
//                         consumer call site.  This fixture (security
//                         gate).
//
// Together: fixtures #1-#3 saturate the federation type-system
// boundary — no malformed permission, no missing permission, no
// cross-org permission can ever reach a federation consumer.
//
// Implementation note: we avoid including FederationProtocol.h here
// because pulling in Refined.h via that path currently exercises a
// GCC 16.1.1 ICE in constexpr-folding of Graded<bool> self-test code
// (segfault inside cp_fold_r on Graded<...>::weaken).  The fixture
// is exactly as load-bearing with a locally-declared Org-B-typed
// stand-in consumer; the substrate's tag-disjoint discipline is on
// the Permission template parameter, not on the surrounding
// function's body.
//
// Expected diagnostic: GCC's "cannot bind reference of type
// 'const Permission<FederatedPeer<OrgB>>&' to
// 'Permission<FederatedPeer<OrgA>>'" or equivalent overload-resolution
// failure.

#include <crucible/fixy/Source.h>

namespace ff = crucible::fixy::source::federation;
namespace cs = crucible::safety;

struct NegFederationCrossOrg_OrgA {};
struct NegFederationCrossOrg_OrgB {};

// Local stand-in for an Org-typed federation consumer.  Mirrors the
// signature shape of cipher::deserialize_federation_entry<Org> —
// takes a `const FederatedPeerPermission<Org>&` as the proof token.
template <typename Org>
inline void typed_federation_consumer(
    const ff::FederatedPeerPermission<Org>& peer_permission) noexcept
{
    (void)peer_permission;
}

int main() {
    // Mint a legitimate OrgA permission via root-mint.  The same type
    // would result from a successful mint_federation_admittance for
    // OrgA.
    auto perm_a = cs::mint_permission_root<
        ff::FederatedPeer<NegFederationCrossOrg_OrgA>>();

    // Cross-org transfer: pass the OrgA-tagged permission to the
    // OrgB-typed consumer.  Must NOT compile — Permission<...> tags
    // are nominal and distinct across Org template parameters.
    typed_federation_consumer<NegFederationCrossOrg_OrgB>(perm_a);
    return 0;
}
