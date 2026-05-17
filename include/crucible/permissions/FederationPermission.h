#pragma once

// ── FederationPermission — typed cross-organization admission ───────
//
// This header is the permission boundary for Cipher federation.  The
// low-level wire codec can still parse bytes as an untrusted frame, but
// consumers that want a trusted federation payload must first mint
// Permission<tag::FederatedPeer<Org>> through the admittance factory.
//
// GAPS-107 decision: V1 federation trust is self-signed peer identity
// fingerprints admitted by an explicit per-org policy.  CA-issued chains and
// web-of-trust are deferred until a real deployment needs those distribution
// semantics.  The actual cryptographic verifier is intentionally outside this
// header; mint_federation_admittance is the substitution point for the HACL*
// verifier and key-distribution discipline.  This layer pins the type-level
// authority flow and the deterministic fingerprint contract the verifier must
// satisfy.
//
// ── fixy-CR-02 (Hunter 5 C1) — INSECURE BY DESIGN, INTENTIONALLY ────
//
// The current `federation_signature_fingerprint(org_id, peer_key_fp,
// nonce)` implementation is a pure deterministic mix64 fold of public
// inputs — no key, no MAC, no asymmetric crypto.  Any caller that
// knows the three public inputs (and `federation_org_id<Org>` is
// `consteval`-reachable from anywhere in the program) can compute a
// valid `self_signature_fingerprint`, mint a forged
// `FederationHandshake`, and pass it through
// `mint_federation_admittance` to obtain
// `Permission<FederatedPeer<Org>>` for ANY organization in the
// admit-policy.  This is acceptable as a V1-development substitution
// point — the type-level authority flow + deterministic fingerprint
// contract are the load-bearing pieces — but the CURRENT verifier is
// trivially forgeable.
//
// The `[[deprecated]]` tag on `mint_federation_admittance` below is
// the compile-time warning.  Production deployment requires
// substituting an HACL*-backed verifier (or equivalent) that takes a
// real per-peer secret key, MACs the handshake, and rejects forged
// signatures.  Until then, every legitimate caller MUST suppress
// `-Wdeprecated-declarations` locally via `_Pragma` to acknowledge
// that they are calling the placeholder.
//
// A positive-attack regression fixture lives at
// `test/safety_attack/attack_federation_forgery.cpp`.  It compiles
// today, runs today, and asserts that forgery succeeds today.  When
// HACL* lands, that assertion will fire and the test will fail
// visibly — flagging that the regression test must be updated AND
// that the deprecation tag can be removed.
//
// ── fixy-CR-03 (Hunter 5 C1) — REPLAY UNRESTRICTED, INTENTIONALLY ───
//
// Orthogonal to forgery (fixy-CR-02), the substrate has no
// replay-protection state.  The admittance verifier checks the
// handshake's structural well-formedness (org match, non-zero peer
// key, non-zero signature, signature equals the deterministic
// mix64 fold) but does NOT check:
//
//   * Has this `(org_id, peer_key_fp, nonce)` triple been admitted
//     before?  (No seen-nonce set.)
//   * Is the handshake within an issuance time window?  (No clock,
//     no expiry field.)
//   * Has the peer's key been rotated or revoked?  (No revocation
//     list; no version-bound binding.)
//   * Is this handshake bound to a specific Cipher epoch or session
//     ID?  (Handshake carries no epoch.)
//
// Consequence: a single captured `FederationHandshake` admits
// forever.  An adversary that observes ONE successful admittance
// (wire sniff, log scrape, sidechannel) can replay the captured
// triple verbatim to mint `Permission<FederatedPeer<Org>>` every
// time, even after legitimate key rotation.  The peer cannot
// "burn" their own nonce — replaying their own handshake from
// last week works identically.
//
// This is acceptable as a V1-development substitution point.
// Production replay protection requires SOME of:
//
//   (a) A per-org `seen_nonces` set persisted in Cipher with GC
//       (eviction by age + size cap).
//   (b) An issuance-time field + monotonic clock + bounded skew
//       window.
//   (c) Cipher-epoch binding — handshakes admit only for the
//       Cipher epoch they were minted in.
//   (d) A challenge-response handshake replacing the unilateral
//       self-signed POD (most invasive — changes the type surface).
//
// (a) is the canonical retrofit and lives behind the same HACL*
// substitution point as fixy-CR-02 — the verifier becomes a
// stateful object holding the seen-nonce set, accepts handshakes
// with cryptographically-fresh nonces, and rejects replays.
//
// A positive-attack regression fixture lives at
// `test/safety_attack/attack_federation_replay.cpp`.  It mints a
// legitimate handshake, calls `mint_federation_admittance` with
// that handshake twice (and a third time months later, simulated),
// and asserts that all three calls succeed today.  When replay
// protection lands, the second-and-later assertions fire — flagging
// that the regression test must be updated AND that the
// replay-protection retrofit is structurally observable from
// existing tests.
//
// ── fixy-CR-04 (Hunter 5 C1) — INSUFFICIENT LOCAL AUTHORITY PROOF ──
//
// Orthogonal to forgery (fixy-CR-02) and replay (fixy-CR-03), the
// `local_permission` parameter is taken by const-ref and discarded
// in the body:
//
//   mint_federation_admittance<Org, Policy>(
//       const LocalCipherPermission& local_permission,  // <-- borrow
//       FederationHandshake handshake) noexcept {
//       (void)local_permission;                          // <-- ignored
//       ...
//   }
//
// The V1 verifier trusts the TYPE of the proof token
// (`Permission<tag::LocalCipherTag>` IS the authority claim per
// CLAUDE.md §XXI Universal Mint Pattern) but never inspects the
// bytes.  The mint therefore accepts:
//
//   * A borrowed const-ref obtained through a singleton accessor
//     by a caller who never minted the permission themselves.
//   * N independent callers sharing the SAME const-ref.  The Linear
//     discipline that makes `Permission<Tag>` move-only does NOT
//     prevent aliasing through `const&`.
//   * A const-ref passed into a function for a different purpose
//     (cipher encryption, say) being repurposed for federation
//     admittance by a deep callee.  Trust-transitivity through
//     ref-passing escalates the proof's authority beyond what the
//     ref's owner intended.
//
// Combined with fixy-CR-02 (forge a handshake from public inputs)
// or fixy-CR-03 (replay a captured handshake), an attacker who
// borrows a const-ref to a LocalCipherPermission anywhere in the
// program has total federation-admittance authority for every org
// in the admit-policy.
//
// Production fix: the HACL*-backed verifier must INSPECT the local
// permission's bytes.  Bind a per-cipher secret into the
// `Permission<LocalCipherTag>`'s storage (real key material, not a
// tag-only proof) and require the handshake's MAC to be computed
// against that secret.  After HACL*, the `(void)local_permission`
// line becomes a real key-extract + MAC-verify; borrowed refs
// pointing to the wrong cipher's material produce MAC mismatches.
// Per-org keying makes the bypass even more structurally
// observable.
//
// A positive-attack regression fixture lives at
// `test/safety_attack/attack_federation_authority_bypass.cpp`.  It
// compiles today, runs today, and asserts that THREE distinct
// borrow patterns (singleton-leak, aliased-N-callers, deep-callee
// trust-transitivity) all succeed today.  When value-based local
// authority lands, the assertions fire — flagging that the
// regression test must be rewritten as a negative regression AND
// that the fixy-CR-04 axis of the [[deprecated]] message can be
// dropped.

#include <crucible/permissions/Permission.h>
#include <crucible/safety/Tagged.h>
#include <crucible/safety/diag/StableName.h>

#include <cstdint>
#include <concepts>
#include <expected>
#include <string_view>
#include <type_traits>

namespace crucible::safety::source {

template <typename Org>
struct FederatedPeer {
    using org_type = Org;
};

}  // namespace crucible::safety::source

namespace crucible::permissions {

namespace tag {

struct LocalCipherTag {};

template <typename Org>
struct FederatedPeer {
    using org_type = Org;
};

}  // namespace tag

template <typename Org>
using FederatedPeerPermission =
    ::crucible::safety::Permission<tag::FederatedPeer<Org>>;

using LocalCipherPermission =
    ::crucible::safety::Permission<tag::LocalCipherTag>;

template <typename Org>
inline constexpr std::uint64_t federation_org_id =
    ::crucible::safety::diag::stable_type_id<Org>;

namespace policy {

template <typename... Orgs>
struct admit_orgs {
    template <typename Org>
    static constexpr bool admits =
        (std::same_as<Org, Orgs> || ...);
};

}  // namespace policy

struct FederationHandshake {
    std::uint64_t org_id = 0;
    std::uint64_t peer_key_fingerprint = 0;
    std::uint64_t nonce = 0;
    std::uint64_t self_signature_fingerprint = 0;
};

static_assert(std::is_trivially_copyable_v<FederationHandshake>);
static_assert(sizeof(FederationHandshake) == 32);

enum class AdmittanceError : std::uint8_t {
    OrgNotAllowed = 1,
    OrgMismatch = 2,
    MissingPeerKey = 3,
    MissingSignature = 4,
    BadSignature = 5,
};

[[nodiscard]] inline constexpr std::string_view
admittance_error_name(AdmittanceError error) noexcept {
    switch (error) {
        case AdmittanceError::OrgNotAllowed:    return "OrgNotAllowed";
        case AdmittanceError::OrgMismatch:      return "OrgMismatch";
        case AdmittanceError::MissingPeerKey:   return "MissingPeerKey";
        case AdmittanceError::MissingSignature: return "MissingSignature";
        case AdmittanceError::BadSignature:     return "BadSignature";
        default:                                return "<unknown AdmittanceError>";
    }
}

namespace detail {

[[nodiscard]] constexpr std::uint64_t federation_mix64(std::uint64_t k) noexcept {
    k ^= k >> 33;
    k *= 0xff51afd7ed558ccdULL;
    k ^= k >> 33;
    k *= 0xc4ceb9fe1a85ec53ULL;
    k ^= k >> 33;
    return k;
}

[[nodiscard]] constexpr std::uint64_t combine_runtime_ids(
    std::uint64_t a,
    std::uint64_t b) noexcept
{
    a ^= b + 0x9e3779b97f4a7c15ULL + (a << 6) + (a >> 2);
    return federation_mix64(a);
}

}  // namespace detail

[[nodiscard]] constexpr std::uint64_t federation_signature_fingerprint(
    std::uint64_t org_id,
    std::uint64_t peer_key_fingerprint,
    std::uint64_t nonce) noexcept
{
    constexpr std::uint64_t kDomain = 0xCFED'AD11'0000'0001ULL;
    return detail::combine_runtime_ids(
        detail::combine_runtime_ids(kDomain, org_id),
        detail::combine_runtime_ids(peer_key_fingerprint, nonce));
}

template <typename Org>
[[nodiscard]] constexpr std::uint64_t default_peer_key_fingerprint() noexcept {
    return detail::combine_runtime_ids(
        federation_org_id<Org>,
        0xCFED'9EED'0000'0001ULL);
}

template <typename Org>
[[nodiscard]] constexpr FederationHandshake
make_self_signed_handshake(
    std::uint64_t peer_key_fingerprint =
        default_peer_key_fingerprint<Org>(),
    std::uint64_t nonce = 0) noexcept
{
    const std::uint64_t org_id = federation_org_id<Org>;
    return FederationHandshake{
        .org_id = org_id,
        .peer_key_fingerprint = peer_key_fingerprint,
        .nonce = nonce,
        .self_signature_fingerprint =
            federation_signature_fingerprint(
                org_id, peer_key_fingerprint, nonce),
    };
}

template <typename Org,
          typename Policy = policy::admit_orgs<Org>>
[[nodiscard,
  deprecated(
      "fixy-CR-02/CR-03/CR-04: self-signed federation handshake is "
      "forgeable (CR-02: signature_fingerprint is a deterministic "
      "mix64 of public values, NOT a MAC), replayable (CR-03: no "
      "seen-nonce / no epoch binding / no expiry), AND admits "
      "borrowed-ref local authority (CR-04: local_permission is "
      "const-ref + (void)-cast — the verifier never inspects the "
      "bytes; any const-ref to a Permission<LocalCipherTag> mints).  "
      "Replace with HACL*-backed verifier (MAC the handshake using "
      "per-cipher secret material) + stateful seen-nonces store "
      "before production deployment.  Suppress locally via "
      "_Pragma(\"GCC diagnostic ignored \\\"-Wdeprecated-declarations\\\"\") "
      "if you are calling this knowingly (tests, V1 development)." )]]
constexpr std::expected<
    FederatedPeerPermission<Org>,
    AdmittanceError>
mint_federation_admittance(
    const LocalCipherPermission& local_permission,
    FederationHandshake handshake) noexcept
{
    (void)local_permission;

    if constexpr (!Policy::template admits<Org>) {
        return std::unexpected(AdmittanceError::OrgNotAllowed);
    }

    if (handshake.org_id != federation_org_id<Org>) {
        return std::unexpected(AdmittanceError::OrgMismatch);
    }
    if (handshake.peer_key_fingerprint == 0) {
        return std::unexpected(AdmittanceError::MissingPeerKey);
    }
    if (handshake.self_signature_fingerprint == 0) {
        return std::unexpected(AdmittanceError::MissingSignature);
    }
    if (handshake.self_signature_fingerprint
        != federation_signature_fingerprint(
            handshake.org_id,
            handshake.peer_key_fingerprint,
            handshake.nonce)) {
        return std::unexpected(AdmittanceError::BadSignature);
    }

    return ::crucible::safety::mint_permission_root<
        tag::FederatedPeer<Org>>();
}

namespace detail::federation_permission_self_test {

struct SelfOrg {};
struct OtherOrg {};

static_assert(federation_org_id<SelfOrg> != 0);
static_assert(federation_org_id<SelfOrg> != federation_org_id<OtherOrg>);
static_assert(policy::admit_orgs<SelfOrg>::template admits<SelfOrg>);
static_assert(!policy::admit_orgs<SelfOrg>::template admits<OtherOrg>);
static_assert(policy::admit_orgs<SelfOrg, OtherOrg>::template admits<OtherOrg>);

static_assert(std::is_same_v<
    FederatedPeerPermission<SelfOrg>::tag_type,
    tag::FederatedPeer<SelfOrg>>);

constexpr FederationHandshake kSelfHandshake =
    make_self_signed_handshake<SelfOrg>(123, 456);
static_assert(kSelfHandshake.org_id == federation_org_id<SelfOrg>);
static_assert(kSelfHandshake.peer_key_fingerprint == 123);
static_assert(kSelfHandshake.self_signature_fingerprint
              == federation_signature_fingerprint(
                  federation_org_id<SelfOrg>, 123, 456));

}  // namespace detail::federation_permission_self_test

}  // namespace crucible::permissions
