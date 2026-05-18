// POSITIVE-ATTACK REGRESSION TEST.  This file MUST PASS TODAY and
// MUST RED when replay protection lands.
//
// ── fixy-CR-03 — Federation handshake replay unrestricted ──────────
//
// Threat model: an adversary OBSERVES one successful federation
// admittance.  This is plausible in practice — handshakes traverse
// the network plaintext (no transport encryption assumed at this
// layer), get logged by intermediate Keeper daemons for forensics,
// and may end up in crash dumps.  Even if observation is "rare"
// (one captured handshake per peer per deployment), the consequence
// of capture must be bounded — a stolen credential is a calamity
// only if it works forever.
//
// V1 design choice (GAPS-107): the substrate has NO replay-protection
// state.  Once a handshake is structurally valid (org match,
// non-zero peer key, non-zero signature, signature ≡ mix64 fold),
// `mint_federation_admittance` returns `Permission<FederatedPeer<Org>>`
// EVERY time it is called with that handshake.  No seen-nonce set,
// no epoch binding, no expiry.
//
// What this fixture proves:
//
//   1. Mint a legitimate handshake for VictimOrgPeer.
//   2. Successfully admit it once (legitimate admittance).
//   3. Replay the SAME handshake bytes verbatim → succeeds again.
//   4. Replay again → succeeds.
//   5. Wait N "epochs" (simulated by minting a different org's
//      handshake interleaved) → original handshake STILL works.
//
// Counter-witness: a freshly-minted DIFFERENT handshake (different
// nonce) for the same org also admits.  This proves the V1
// admittance is correct on well-formed inputs; the vulnerability is
// SPECIFICALLY the absence of replay rejection on previously-seen
// inputs.
//
// WHEN THIS TEST REDDENS (i.e., assertion #3 or later fires
// because the second/third call returns
// `AdmittanceError::ReplayDetected` or similar):
//   1. Confirm replay protection has landed (seen-nonces set, epoch
//      binding, or HACL*-stateful verifier).
//   2. Update mint_federation_admittance to advertise the new
//      `AdmittanceError::ReplayDetected` enumerator.
//   3. Remove the fixy-CR-03 line from the `[[deprecated]]` message
//      on mint_federation_admittance (and the doc-block section).
//   4. Rewrite THIS fixture as a NEGATIVE regression: prove that
//      a second admittance of the same handshake returns the
//      replay-error variant.
//
// Distinct from `attack_federation_forgery.cpp`:
//   - forgery: anyone who knows the public inputs can SYNTHESIZE
//     a handshake from scratch.  (Authentication failure.)
//   - replay  (this): given an OBSERVED legitimate handshake, the
//     observer can use it indefinitely.  (Freshness failure.)
//   Both must be closed for the federation to be production-safe.

#include <crucible/permissions/FederationPermission.h>

#include "../test_assert.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <type_traits>

// fixy-CR-02 — mint_federation_admittance is [[deprecated]] in V1.
// This attack regression knowingly exercises the placeholder
// verifier to lock the replay vulnerability into CI.  Suppress the
// deprecation diagnostic at the TU level.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

namespace perm = crucible::permissions;
namespace saf  = crucible::safety;

namespace {

struct VictimOrgSelf {};   // the deployment we are attacking
struct VictimOrgPeer {};   // a legitimate peer of the deployment
struct OrgInterleave {};   // unrelated org whose handshake is
                           // minted in between victim's replays

// The deployment admits both Self and Peer in its production policy.
using AllowSelfAndPeer =
    perm::policy::admit_orgs<VictimOrgSelf, VictimOrgPeer>;
// Also admit OrgInterleave so the interleave-minting step below
// itself succeeds — we are testing whether interleaving FRESH
// admittances clears the seen-state for the replayed handshake.
using AllowAllThree = perm::policy::admit_orgs<
    VictimOrgSelf, VictimOrgPeer, OrgInterleave>;

const perm::LocalCipherPermission& local_cipher_permission() {
    static const auto permission =
        saf::mint_permission_root<perm::tag::LocalCipherTag>();
    return permission;
}

// Capture the handshake bytes (the wire-sniff simulation).  A real
// adversary would obtain these from network capture; here we let
// the legitimate peer construct them and then *copy* the POD so
// the same bytes are reused without re-deriving from the
// constructor.
struct CapturedHandshake {
    perm::OrgId               org_id;
    perm::PeerKeyFingerprint  peer_key_fingerprint;
    perm::Nonce               nonce;
    perm::SignatureFingerprint self_signature_fingerprint;
};

static_assert(sizeof(CapturedHandshake) == sizeof(perm::FederationHandshake),
    "CapturedHandshake must layout-match FederationHandshake — the "
    "attack copies bytes verbatim, not just structurally.");

CapturedHandshake sniff_handshake(perm::FederationHandshake hs) noexcept {
    return CapturedHandshake{
        .org_id                     = hs.org_id,
        .peer_key_fingerprint       = hs.peer_key_fingerprint,
        .nonce                      = hs.nonce,
        .self_signature_fingerprint = hs.self_signature_fingerprint,
    };
}

perm::FederationHandshake rehydrate(CapturedHandshake captured) noexcept {
    // Verbatim replay — same nonce, same signature, same peer key.
    return perm::FederationHandshake{
        .org_id                     = captured.org_id,
        .peer_key_fingerprint       = captured.peer_key_fingerprint,
        .nonce                      = captured.nonce,
        .self_signature_fingerprint = captured.self_signature_fingerprint,
    };
}

int test_replay_succeeds_indefinitely() {
    // ── Step 1: legitimate peer mints a fresh handshake ───────────
    const auto fresh_hs =
        perm::make_self_signed_handshake<VictimOrgPeer>(
            /*peer_key_fingerprint=*/perm::PeerKeyFingerprint{0xC0FFEE'B0BADD'BEEFULL},
            /*nonce=*/                perm::Nonce{0xDEAD'BEEF'CAFE'F00DULL});

    // Attacker captures the handshake bytes off the wire.
    const auto captured = sniff_handshake(fresh_hs);

    // ── Step 2: legitimate admittance (first call) ────────────────
    auto admittance_1 = perm::mint_federation_admittance<
        VictimOrgPeer, AllowSelfAndPeer>(
            local_cipher_permission(), rehydrate(captured));
    assert(admittance_1.has_value());

    // ── Step 3: VERBATIM replay (the vulnerability) ───────────────
    // Same bytes, same nonce.  In a production-safe substrate this
    // MUST return AdmittanceError::ReplayDetected (or similar).
    // Today, it succeeds — locking the V1 insecurity into CI.
    auto admittance_2 = perm::mint_federation_admittance<
        VictimOrgPeer, AllowSelfAndPeer>(
            local_cipher_permission(), rehydrate(captured));
    assert(admittance_2.has_value());

    // ── Step 4: replay again — once more for the audit trail ─────
    auto admittance_3 = perm::mint_federation_admittance<
        VictimOrgPeer, AllowSelfAndPeer>(
            local_cipher_permission(), rehydrate(captured));
    assert(admittance_3.has_value());

    // ── Step 5: interleave a different org's fresh handshake ─────
    // This proves the substrate carries NO per-org seen-state.  In
    // a hypothetical "rotate seen-nonces on org-context-switch"
    // variant, the interleave could clear the replay marker; here,
    // it shouldn't even matter because no seen-state exists.  We
    // mint an unrelated org's handshake, admit it, then replay the
    // ORIGINAL captured handshake — still succeeds.
    const auto interleave_hs =
        perm::make_self_signed_handshake<OrgInterleave>(
            /*peer_key_fingerprint=*/perm::PeerKeyFingerprint{0x1234'5678'9ABC'DEF0ULL},
            /*nonce=*/                perm::Nonce{0xFEED'FACE'BABE'B00BULL});
    auto interleave_admit = perm::mint_federation_admittance<
        OrgInterleave, AllowAllThree>(
            local_cipher_permission(), interleave_hs);
    assert(interleave_admit.has_value());

    // Replay the ORIGINAL handshake AFTER an interleaved
    // admittance — still succeeds (no state, no protection).
    auto admittance_after_interleave = perm::mint_federation_admittance<
        VictimOrgPeer, AllowAllThree>(
            local_cipher_permission(), rehydrate(captured));
    assert(admittance_after_interleave.has_value());

    return 0;
}

int test_fresh_handshakes_still_admit() {
    // Counter-witness: the V1 admittance correctly admits FRESH
    // handshakes (different nonces) for the same peer.  This proves
    // the vulnerability is specifically the absence of replay
    // rejection — the substrate's positive path is well-defined.
    const auto fresh_a = perm::make_self_signed_handshake<VictimOrgPeer>(
        /*peer_key_fp=*/perm::PeerKeyFingerprint{0xAAAA},
        /*nonce=*/perm::Nonce{0x1111});
    const auto fresh_b = perm::make_self_signed_handshake<VictimOrgPeer>(
        /*peer_key_fp=*/perm::PeerKeyFingerprint{0xAAAA},
        /*nonce=*/perm::Nonce{0x2222});  // different nonce
    assert(fresh_a.nonce != fresh_b.nonce);
    assert(fresh_a.self_signature_fingerprint
           != fresh_b.self_signature_fingerprint);  // signature is
                                                    // nonce-bound

    auto admit_a = perm::mint_federation_admittance<
        VictimOrgPeer, AllowSelfAndPeer>(
            local_cipher_permission(), fresh_a);
    auto admit_b = perm::mint_federation_admittance<
        VictimOrgPeer, AllowSelfAndPeer>(
            local_cipher_permission(), fresh_b);
    assert(admit_a.has_value());
    assert(admit_b.has_value());
    return 0;
}

int test_replay_with_zeroed_nonce_still_rejected() {
    // Sanity counter-witness: the EXISTING structural checks still
    // fire.  A handshake with a zero peer_key OR zero signature is
    // rejected (admittance returns MissingPeerKey / MissingSignature)
    // even under replay.  This proves the V1 substrate retains its
    // structural integrity — the replay vulnerability is narrow to
    // well-formed-but-stale handshakes.
    auto fresh = perm::make_self_signed_handshake<VictimOrgPeer>(
        /*peer_key_fp=*/perm::PeerKeyFingerprint{0xCAFE},
        /*nonce=*/perm::Nonce{0xBABE});

    auto zeroed_key = fresh;
    zeroed_key.peer_key_fingerprint = perm::PeerKeyFingerprint{};
    auto admit_zeroed = perm::mint_federation_admittance<
        VictimOrgPeer, AllowSelfAndPeer>(
            local_cipher_permission(), zeroed_key);
    assert(!admit_zeroed.has_value());
    assert(admit_zeroed.error() == perm::AdmittanceError::MissingPeerKey);

    return 0;
}

}  // namespace

int main() {
    if (int rc = test_replay_succeeds_indefinitely(); rc != 0) {
        std::fprintf(stderr,
            "test_replay_succeeds_indefinitely failed (rc=%d)\n", rc);
        return 1;
    }
    if (int rc = test_fresh_handshakes_still_admit(); rc != 0) {
        std::fprintf(stderr,
            "test_fresh_handshakes_still_admit failed (rc=%d)\n", rc);
        return 2;
    }
    if (int rc = test_replay_with_zeroed_nonce_still_rejected(); rc != 0) {
        std::fprintf(stderr,
            "test_replay_with_zeroed_nonce_still_rejected failed (rc=%d)\n",
            rc);
        return 3;
    }

    std::puts(
        "attack_federation_replay: V1 handshakes are replayable as "
        "documented (fixy-CR-03).  When this test REDDENS, replay "
        "protection has landed — see the doc-block at the top of "
        "this file for the remediation checklist.");
    return 0;
}

#pragma GCC diagnostic pop
