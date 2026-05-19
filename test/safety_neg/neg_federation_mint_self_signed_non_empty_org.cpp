// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-L-02 #1518 negative fixture #2/2:
// `permissions::mint_self_signed_handshake<Org>(...)` requires
// `FederationOrgTag<Org>`, which demands `std::is_empty_v<Org>`
// AFTER `std::is_class_v<Org>` succeeds.  Calling the mint with
// a NON-empty class (here: a class carrying a data member) fails
// the concept gate at the `requires`-clause.
//
// Distinct from fixture #1 (non-class-org): #2 exercises the
// emptiness half of FederationOrgTag — a class shape that LOOKS
// like a federation tag but carries instance state, which is
// the canonical copy-paste-from-runtime-variable footgun the
// FederationOrgTag concept gate is designed to catch.
//
// Expected diagnostic: constraints not satisfied / no matching
// function / FederationOrgTag / std::is_empty_v.

#include <crucible/permissions/FederationPermission.h>

namespace perm = ::crucible::permissions;

namespace neg_fed_non_empty_org {
// Non-empty class — has a data member, so std::is_empty_v is false.
// Distinct from the empty marker types that ALL shipped federation
// orgs use (SelfOrg / OrgPeer / OrgA / OrgBlocked / ...).
struct NonEmptyOrg {
    int sentinel_field = 0;
};
}

int main() {
    // Should FAIL: `NonEmptyOrg` is a class but NOT empty →
    // FederationOrgTag<NonEmptyOrg> is false → mint_self_signed_
    // handshake<NonEmptyOrg> requires-clause rejects the
    // substitution.
    auto handshake = perm::mint_self_signed_handshake<
        neg_fed_non_empty_org::NonEmptyOrg>(
        perm::PeerKeyFingerprint{0xDEADBEEFULL},
        perm::Nonce{0xC0FFEEULL});
    (void)handshake;
    return 0;
}
