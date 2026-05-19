// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-L-02 #1518 negative fixture #1/2:
// `permissions::mint_self_signed_handshake<Org>(...)` requires
// `FederationOrgTag<Org>`, which demands `std::is_class_v<Org>`.
// Calling the mint with a non-class type (here: `int`) fails the
// concept gate at the `requires`-clause; substitution fails, no
// implicit cast bridges to a class type.
//
// Pairs with neg_federation_mint_self_signed_non_empty_org.cpp
// (distinct rejection class: non-empty class fails the
// `std::is_empty_v<Org>` half of the same concept).  Together the
// two fixtures discharge HS14's ≥2-distinct-mismatch floor on the
// `mint_self_signed_handshake` §XXI mint gate.
//
// Expected diagnostic: constraints not satisfied / no matching
// function / FederationOrgTag / std::is_class_v.

#include <crucible/permissions/FederationPermission.h>

namespace perm = ::crucible::permissions;

int main() {
    // Should FAIL: `int` is not a class type → FederationOrgTag<int>
    // is false → mint_self_signed_handshake<int> requires-clause
    // rejects the substitution.
    auto handshake = perm::mint_self_signed_handshake<int>(
        perm::PeerKeyFingerprint{0xDEADBEEFULL},
        perm::Nonce{0xC0FFEEULL});
    (void)handshake;
    return 0;
}
