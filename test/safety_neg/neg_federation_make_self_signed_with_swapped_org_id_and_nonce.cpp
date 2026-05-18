// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A1-008 HS14 fixture: strong-type discipline on
// FederationHandshake field assignment.  The substrate POD is
//
//   struct FederationHandshake {
//       OrgId                org_id{};
//       PeerKeyFingerprint   peer_key_fingerprint{};
//       Nonce                nonce{};
//       SignatureFingerprint self_signature_fingerprint{};
//   };
//
// Each field is a CRUCIBLE_STRONG_HASH-style newtype with explicit
// single-argument constructor.  Pre-migration the fields were bare
// uint64_t — a caller could assign a raw integer literal directly to
// any field, and a refactor that flipped a literal between fields
// (org_id ↔ nonce, for instance) silently changed the handshake's
// admittance domain without diagnostic.
//
// This fixture exercises two violations in one TU:
//   1. Direct raw-integer assignment to a strong-typed field
//      (pre-migration: legal; post-migration: type mismatch).
//   2. Cross-domain field assignment (Nonce → org_id slot).
//
// Either violation alone is sufficient for the type system to
// reject the TU.  Both together pin the discipline at the field
// level.
//
// Expected diagnostic: "cannot convert", "no matching function",
// "conversion from", "no known conversion".

#include <crucible/permissions/FederationPermission.h>

namespace perm = ::crucible::permissions;

struct NegSwappedFields_Org {};

int main() {
    // Start with a well-formed handshake so the failures below are
    // about the field assignments, not the construction.
    auto hs = perm::make_self_signed_handshake<NegSwappedFields_Org>(
        /*peer_key_fp=*/perm::PeerKeyFingerprint{0xAAAA},
        /*nonce=*/      perm::Nonce{0x1111});

    // VIOLATION 1: raw integer literal cannot implicitly convert to
    // OrgId — the explicit constructor on the newtype rejects this.
    hs.org_id = 0xBEEF'BEEFULL;

    // VIOLATION 2: cross-domain field write — assigning a Nonce to
    // the org_id slot was a silent footgun under the bare-uint64_t
    // shape; the strong types reject it.
    perm::Nonce wrong_domain{0xCCCCULL};
    hs.org_id = wrong_domain;

    (void)hs;
    return 0;
}
