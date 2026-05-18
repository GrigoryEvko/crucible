// ── attack_federation_forgery — fixy-CR-02 positive-attack regression
//
// REGRESSION-VISIBLE INSECURITY WITNESS.
//
// This test asserts that the V1 self-signed federation handshake is
// trivially forgeable today.  It compiles today, runs today, and
// PASSES today because the asserted forgery succeeds.  When HACL*
// (or any real cryptographic MAC) lands behind
// mint_federation_admittance, the forgery WILL fail to produce a
// valid Permission<FederatedPeer<Org>> from public inputs alone, and
// this test WILL flip from green to red.
//
// THE RED SIGNAL IS LOAD-BEARING.  When this test reddens:
//
//   1. Update mint_federation_admittance to expect the new
//      verifier's per-peer secret-key input.
//   2. Remove the [[deprecated]] tag on mint_federation_admittance
//      (substrate-header FederationPermission.h §fixy-CR-02 doc-block).
//   3. Remove the corresponding `#pragma GCC diagnostic ignored
//      "-Wdeprecated-declarations"` suppressions in:
//        - test/test_federation_permission.cpp
//        - test/test_fixy_source_federation.cpp
//        - test/fixy_neg/neg_fixy_federation_missing_proof.cpp
//        - test/fixy_neg/neg_fixy_federation_wrong_org.cpp
//   4. Convert THIS test from an attack-witness into a regression
//      test that the forgery is REJECTED (i.e., invert every
//      `assert(result.has_value())` to `assert(!result.has_value())`
//      and update the doc-block).
//
// The forgery primitive
// ─────────────────────
//
// `perm::federation_signature_fingerprint(org_id, peer_key_fp, nonce)`
// is a pure deterministic mix64 fold of public inputs — there is no
// MAC, no asymmetric key, no shared secret.  Any caller that knows
// these three inputs (and `federation_org_id<Org>` is consteval-
// reachable from any TU in the program) can compute a valid
// `self_signature_fingerprint` and hand it through
// `mint_federation_admittance<Org, Policy>` to obtain
// `Permission<FederatedPeer<Org>>` for ANY org in the admit-policy.
//
// We exercise the forgery against a victim policy
// `admit_orgs<VictimOrgSelf, VictimOrgPeer>` from an attacker TU that
// (a) has access to the substrate header (necessarily — federation
// admission must be reachable from any peer TU) and (b) does NOT
// hold any per-peer secret key (since none exists in V1).
//
// The forged permission is then used to round-trip the federation
// cache deserialize step, producing a Tagged<...,FederatedPeer<Org>>
// view of attacker-controlled payload bytes.  This proves the entire
// federation trust chain collapses to "knows the org_id" — i.e.,
// knows the program at all.
//
// See: include/crucible/permissions/FederationPermission.h §fixy-CR-02

#include <crucible/cipher/ComputationCacheFederation.h>
#include <crucible/permissions/FederationPermission.h>
#include <crucible/safety/IsTagged.h>

// fixy-CR-02 — mint_federation_admittance is [[deprecated]] in V1.
// This file intentionally exercises the placeholder verifier
// to prove the forgery succeeds.  Suppress the deprecation
// diagnostic so the regression-witness compiles cleanly with
// -Werror=deprecated-declarations.  When HACL* lands and this
// file flips red, the suppression below comes out together with
// the deprecation tag on mint_federation_admittance.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include "../test_assert.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <span>

namespace fed  = crucible::cipher::federation;
namespace eff  = crucible::effects;
namespace perm = crucible::permissions;
namespace saf  = crucible::safety;

namespace {

// ─── The victim ────────────────────────────────────────────────────
//
// A federation deployment that admits two organizations — the
// operator's own org (VictimOrgSelf) and a single declared peer
// (VictimOrgPeer).  Realistic V1 admit-policy shape.
struct VictimOrgSelf {};
struct VictimOrgPeer {};

using VictimAdmitPolicy =
    perm::policy::admit_orgs<VictimOrgSelf, VictimOrgPeer>;

inline void f_attacker_payload(int) noexcept {}

using RowIO = eff::Row<eff::Effect::IO>;

const perm::LocalCipherPermission& local_cipher_permission() {
    static const auto p =
        saf::mint_permission_root<perm::tag::LocalCipherTag>();
    return p;
}

// ─── The attack ────────────────────────────────────────────────────
//
// Step 1.  The attacker knows the org_id of the peer it wants to
//          impersonate.  `federation_org_id<Org>` is consteval-
//          reachable from any TU that includes the substrate header,
//          so VictimOrgPeer's org_id is `0x...` (whatever
//          stable_type_id<VictimOrgPeer> resolves to).  Same code
//          path the legitimate operator uses to declare the peer.
//
// Step 2.  The attacker picks an arbitrary nonce and an arbitrary
//          peer_key_fingerprint.  In V1 the verifier accepts any
//          non-zero values for these — they are not validated
//          against any registered per-peer key.
//
// Step 3.  The attacker computes
//          `federation_signature_fingerprint(org_id, peer_key_fp,
//          nonce)` — a public, deterministic, key-less mix64 fold.
//
// Step 4.  The attacker assembles a `FederationHandshake` and hands
//          it to `mint_federation_admittance<VictimOrgPeer,
//          VictimAdmitPolicy>`.  The verifier accepts and emits
//          `Permission<FederatedPeer<VictimOrgPeer>>`.
//
// Step 5.  The attacker uses the minted permission to deserialize
//          a federation-cache entry of attacker-controlled bytes
//          tagged as having come from VictimOrgPeer.

[[nodiscard]] perm::FederationHandshake
forge_handshake_for_peer(perm::Nonce attacker_nonce,
                         perm::PeerKeyFingerprint attacker_peer_key_fp)
{
    const perm::OrgId victim_peer_org_id =
        perm::federation_org_id<VictimOrgPeer>;

    return perm::FederationHandshake{
        .org_id = victim_peer_org_id,
        .peer_key_fingerprint = attacker_peer_key_fp,
        .nonce = attacker_nonce,
        .self_signature_fingerprint =
            perm::federation_signature_fingerprint(
                victim_peer_org_id,
                attacker_peer_key_fp,
                attacker_nonce),
    };
}

int test_attacker_mints_forged_peer_permission() {
    // The forgery — every input is public, attacker-chosen.
    const auto forged_hs = forge_handshake_for_peer(
        /*attacker_nonce=*/perm::Nonce{0xDEAD'BEEFu},
        /*attacker_peer_key_fp=*/perm::PeerKeyFingerprint{0xCAFE'BABEu});

    auto forged =
        perm::mint_federation_admittance<VictimOrgPeer,
                                         VictimAdmitPolicy>(
            local_cipher_permission(), forged_hs);

    // V1 INSECURITY: the forged handshake passes the gate.
    // When HACL* lands, the forgery will return AdmittanceError::
    // BadSignature (or MissingPeerKey if the verifier demands a
    // registered per-peer key) and this assertion will fail.
    assert(forged.has_value());
    return 0;
}

int test_attacker_round_trips_federation_cache_entry() {
    // Attacker writes a federation cache entry under whatever
    // local-cipher authority the program holds (this is the
    // surface that the federation-cache substrate accepts) ...
    std::array<std::uint8_t, 64> buf{};
    const std::array<std::uint8_t, 4> attacker_payload = {
        0xAB, 0xCD, 0xEF, 0x42,
    };

    auto written = fed::serialize_computation_cache_federation_entry<
        &f_attacker_payload, RowIO, int>(
        local_cipher_permission(), buf, attacker_payload);
    assert(written.has_value());

    // ... then mints a forged peer permission for VictimOrgPeer.
    const auto forged_hs = forge_handshake_for_peer(
        /*attacker_nonce=*/perm::Nonce{0xF00D'1234u},
        /*attacker_peer_key_fp=*/perm::PeerKeyFingerprint{0x9876'5432u});
    auto forged =
        perm::mint_federation_admittance<VictimOrgPeer,
                                         VictimAdmitPolicy>(
            local_cipher_permission(), forged_hs);
    assert(forged.has_value());

    // ... and round-trips the entry through deserialize using the
    // forged permission, obtaining a Tagged<...,FederatedPeer<
    // VictimOrgPeer>> view of attacker-controlled bytes.  At this
    // point the runtime believes VictimOrgPeer is the source of
    // `attacker_payload`.  This is the trust-boundary collapse
    // fixy-CR-02 describes.
    auto tagged_view = fed::deserialize_federation_entry(
        *forged,
        std::span<const std::uint8_t>(buf.data(), *written),
        static_cast<std::uint16_t>(eff::OsUniverse::cardinality));
    assert(tagged_view.has_value());

    using TaggedView = std::remove_cvref_t<decltype(*tagged_view)>;
    static_assert(saf::extract::is_tagged_v<TaggedView>);
    static_assert(std::is_same_v<
        saf::extract::tagged_tag_t<TaggedView>,
        saf::source::FederatedPeer<VictimOrgPeer>>);

    const auto& view = tagged_view->value();
    assert(view.payload.size() == attacker_payload.size());
    for (std::size_t i = 0; i < attacker_payload.size(); ++i) {
        assert(view.payload[i] == attacker_payload[i]);
    }

    return 0;
}

// ─── Sanity counter-witness ────────────────────────────────────────
//
// The attack does NOT pierce the org-admit policy itself.  A peer
// the policy does not admit is still rejected at compile time
// (Policy::admits<Org> is consteval).  The forgery only breaks
// the per-peer signature gate — anyone in the admit-policy can
// impersonate anyone else in the admit-policy.

struct OrgOutsidePolicy {};

int test_admit_policy_still_filters_unlisted_orgs() {
    auto forged_outside =
        perm::mint_federation_admittance<OrgOutsidePolicy,
                                         VictimAdmitPolicy>(
            local_cipher_permission(),
            perm::make_self_signed_handshake<OrgOutsidePolicy>(
                /*peer_key_fingerprint=*/perm::PeerKeyFingerprint{0xABCDu},
                /*nonce=*/perm::Nonce{0}));
    assert(!forged_outside.has_value());
    assert(forged_outside.error()
           == perm::AdmittanceError::OrgNotAllowed);
    return 0;
}

}  // namespace

int main() {
    if (int rc = test_attacker_mints_forged_peer_permission();
        rc != 0) {
        return rc;
    }
    if (int rc = test_attacker_round_trips_federation_cache_entry();
        rc != 0) {
        return 100 + rc;
    }
    if (int rc = test_admit_policy_still_filters_unlisted_orgs();
        rc != 0) {
        return 200 + rc;
    }

    std::puts(
        "attack_federation_forgery: V1 self-signed handshake is "
        "forgeable as documented (fixy-CR-02).  When this test "
        "REDDENS, the HACL* verifier has landed — remove "
        "[[deprecated]] tag and pragma suppressions per the "
        "doc-block.");
    return 0;
}

#pragma GCC diagnostic pop
