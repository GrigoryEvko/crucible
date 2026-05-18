// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A1-008 HS14 fixture: strong-type discipline on
// federation_signature_fingerprint.  The substrate-level signature is
//
//   constexpr SignatureFingerprint
//   federation_signature_fingerprint(
//       OrgId org_id,
//       PeerKeyFingerprint peer_key_fingerprint,
//       Nonce nonce) noexcept;
//
// Each parameter is a CRUCIBLE_STRONG_HASH-style newtype with an
// explicit single-argument constructor and NO implicit conversion
// from sibling types.  This fixture proves that the type system
// rejects the canonical "I swapped the first two args" footgun —
// passing a PeerKeyFingerprint where OrgId is expected and vice
// versa.  The pre-migration uint64_t signature would have compiled
// silently and produced an off-by-domain signature, breaking
// federation admittance in subtle ways.
//
// Expected diagnostic: "cannot convert", "no matching function",
// "conversion from", "no known conversion".

#include <crucible/permissions/FederationPermission.h>

namespace perm = ::crucible::permissions;

struct NegSwap_Org {};

int main() {
    // Build well-formed inputs for the three parameter positions.
    constexpr auto org_id   = perm::federation_org_id<NegSwap_Org>;
    constexpr perm::PeerKeyFingerprint key{0xC0FFEE'C0FFEEULL};
    constexpr perm::Nonce              nonce{0xC1C1'C1C1ULL};

    // VIOLATION: swap positions 0 and 1.  The first arg expects
    // OrgId, but we pass a PeerKeyFingerprint.  The second arg
    // expects PeerKeyFingerprint, but we pass an OrgId.  Both
    // mismatches are required for the rejection to be unambiguous.
    auto sig =
        perm::federation_signature_fingerprint(key, org_id, nonce);
    (void)sig;
    return 0;
}
