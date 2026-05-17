// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-AUDIT-D5 fixture #1: mint_federation_admittance rejects when
// the caller passes a Permission of the wrong type as the
// `local_permission` argument.  The substrate signature is
//
//   mint_federation_admittance<Org, Policy>(
//       const LocalCipherPermission& local_permission,
//       FederationHandshake handshake) noexcept
//
// where `LocalCipherPermission = Permission<tag::LocalCipherTag>`.
// Permission<Tag> is move-only and has no implicit conversion across
// tags — passing a `Permission<OtherTag>` is a hard type-mismatch
// compile error.
//
// Violation: build a `Permission<tag::FederatedPeer<OrgA>>` (NOT a
// LocalCipherPermission) and attempt to use it as the first arg.
// The substrate refuses because the permission tag must be the
// distinguished `tag::LocalCipherTag` carrier (CLAUDE.md §XXI mint
// discipline: the proof token's type carries the authority claim).
//
// Expected diagnostic: GCC emits a "cannot bind reference of type
// 'const Permission<LocalCipherTag>&' to 'Permission<FederatedPeer<...>>'"
// (or an equivalent overload-resolution failure naming the substrate
// function signature).

#include <crucible/fixy/Source.h>

namespace ff = crucible::fixy::source::federation;
namespace cs = crucible::safety;

struct NegFederationWrongOrg_OrgA {};

int main() {
    // A FederatedPeerPermission<OrgA> is NOT a LocalCipherPermission.
    // mint_federation_admittance demands the LocalCipherTag-typed
    // proof token; the wrong-tag permission must fail type matching.
    auto wrong = cs::mint_permission_root<
        ff::FederatedPeer<NegFederationWrongOrg_OrgA>>();

    auto handshake = ff::make_self_signed_handshake<
        NegFederationWrongOrg_OrgA>();

    // This call must NOT compile — `wrong` is the wrong Permission tag.
    (void)ff::mint_federation_admittance<NegFederationWrongOrg_OrgA>(
        wrong, handshake);
    return 0;
}
