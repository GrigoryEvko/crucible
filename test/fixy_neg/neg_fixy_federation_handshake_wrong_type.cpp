// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-AUDIT-G8 fixture #4: mint_federation_admittance rejects a
// non-FederationHandshake value in the handshake slot.
//
// Scenario: a caller bypasses the structured handshake-construction
// flow (make_self_signed_handshake) and passes a raw POD struct in
// the handshake slot — perhaps a partially-constructed object or a
// different protocol's handshake type.  The substrate signature is
//
//   mint_federation_admittance<Org, Policy>(
//       const LocalCipherPermission& local_permission,
//       FederationHandshake handshake) noexcept
//
// where `handshake` is taken BY VALUE of the exact type
// FederationHandshake.  A different struct type cannot bind even if
// it is layout-compatible, because C++ has no structural typing for
// distinct class types.
//
// Distinct from fixtures #1-#3:
//   #1 wrong_org              — wrong proof tag (FederatedPeer vs LocalCipher)
//   #2 missing_proof          — proof slot has plain int
//   #3 cross_org_transfer     — consumer-side cross-org tag mismatch
//   #4 handshake_wrong_type   — HANDSHAKE slot has a non-FederationHandshake
//                                struct.  This fixture (handshake-side gate).
//
// Together with fixtures #1-#3, this saturates the mint_federation_-
// admittance boundary across BOTH parameter axes (proof × handshake):
//
//                    | correct proof    | wrong proof
//   ────────────────  ───────────────────────────────
//   correct handshake | (positive case) | #1 / #2
//   wrong handshake   | #4 (this)       | (subsumed)
//
// Expected diagnostic: GCC's "cannot convert argument" /
// "no matching function" overload-resolution failure at the handshake
// parameter.

#include <crucible/fixy/Source.h>

namespace ff = crucible::fixy::source::federation;
namespace cs = crucible::safety;

struct NegFederationHandshakeType_Org {};

// A POD that is NOT FederationHandshake — it has different field
// layout / different type identity.  No implicit conversion exists.
struct NegFederationHandshakeType_FakeHandshake {
    int  org_id = 0;
    long peer_key_fingerprint = 0;
};

int main() {
    // Mint a legitimate local cipher permission for the proof slot —
    // proves the gate is on the HANDSHAKE parameter, not the proof.
    auto local_perm = cs::mint_permission_root<ff::LocalCipherTag>();

    NegFederationHandshakeType_FakeHandshake fake_hs{};

    // Wrong handshake type — must NOT compile.  The substrate signature
    // takes FederationHandshake by value; a distinct POD class cannot
    // bind even if it has structurally similar fields.
    (void)ff::mint_federation_admittance<NegFederationHandshakeType_Org>(
        local_perm, fake_hs);
    return 0;
}
