// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A1-008 HS14 fixture: strong-type discipline on
// make_self_signed_handshake.  The substrate signature is
//
//   template <typename Org>
//   constexpr FederationHandshake
//   make_self_signed_handshake(
//       PeerKeyFingerprint peer_key_fingerprint = ...,
//       Nonce nonce = Nonce{0}) noexcept;
//
// Pre-migration the parameters were bare uint64_t pair, indistinguishable
// at the type level.  A caller who swapped them silently produced a
// handshake with the wrong domain in each slot — the
// federation_signature_fingerprint subsequently folded the wrong
// values into self_signature_fingerprint and the deployment's
// admittance verifier accepted it (since both sides used the same
// scrambled domains).  This is the canonical TypeSafe-axiom defect
// the migration closes.
//
// VIOLATION: pass a Nonce where PeerKeyFingerprint is expected.  The
// strong-typed signature requires both arguments to be of the right
// domain; the implicit-explicit constructor on each newtype rejects
// the cross-domain conversion.
//
// Expected diagnostic: "cannot convert", "no matching function",
// "conversion from", "no known conversion".

#include <crucible/permissions/FederationPermission.h>

namespace perm = ::crucible::permissions;

struct NegPassedNonce_Org {};

int main() {
    constexpr perm::Nonce              nonce{0xDEADBEEFULL};
    constexpr perm::PeerKeyFingerprint key  {0xCAFEBABEULL};

    // VIOLATION: Nonce in PeerKeyFingerprint position, PeerKeyFingerprint
    // in Nonce position.  The first parameter expects
    // PeerKeyFingerprint and rejects Nonce; the second expects Nonce
    // and rejects PeerKeyFingerprint.  Either rejection alone fires
    // the static type-system gate.
    auto hs = perm::make_self_signed_handshake<NegPassedNonce_Org>(
        /*peer_key_fp=*/nonce,
        /*nonce=*/      key);
    (void)hs;
    return 0;
}
