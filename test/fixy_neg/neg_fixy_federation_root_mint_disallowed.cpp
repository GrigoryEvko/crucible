// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-CR-06 fixture: `::crucible::safety::mint_permission_root<Tag>()`
// (no-ctx variant) is concept-deleted for any Tag matching
// `is_federated_peer_tag_v<Tag>`.  Federation peer admittance is the
// load-bearing security boundary between organizations; minting a
// Permission<tag::FederatedPeer<Org>> without running the admittance
// policy + handshake check would bypass the federation trust model
// entirely.
//
// The legitimate path is `mint_federation_admittance<Org, Policy>(
// local_cipher, handshake)` — the only non-deleted constructor of a
// federation peer Permission.  This fixture proves a stray
// `mint_permission_root<FederatedPeer<X>>()` call site reds the
// build at the call site, with the reason string visible to the
// programmer.
//
// Expected diagnostic: GCC's "use of deleted function" naming
// `mint_permission_root` and citing the fixy-CR-06 reason string.

#include <crucible/permissions/FederationPermission.h>

namespace perm = crucible::permissions;
namespace saf  = crucible::safety;

struct NegRootMint_OrgA {};

int main() {
    // The deleted overload wins concept-ordering against the generic
    // `mint_permission_root<Tag>()` template because
    // `is_federated_peer_tag_v<tag::FederatedPeer<OrgA>>` is true.
    // Must NOT compile.
    auto bad = saf::mint_permission_root<
        perm::tag::FederatedPeer<NegRootMint_OrgA>>();
    (void)bad;
    return 0;
}
