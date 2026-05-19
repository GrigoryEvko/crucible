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
//
// ── fixy-CR-05 (Hunter 5 C4) — CROSS-ORG SPLIT ESCALATION ──────────
//
// `splits_into<Parent, L, R>` in `permissions/Permission.h` is a
// USER-EXTENSIBLE trait by design — the docstring says
// "specializations belong in the SAME TU as the tags", but that is
// a review-only discipline with zero structural machinery enforcing
// it.  Any downstream TU can write:
//
//   namespace crucible::safety {
//   template <> struct splits_into<
//       tag::FederatedPeer<OrgA>,
//       tag::FederatedPeer<OrgB>,
//       tag::FederatedPeer<OrgB>>
//       : std::true_type {};
//   }
//
// A full specialization with three concrete types is MORE
// specialized than any partial specialization we ship in this
// header (C++ partial-specialization ranking).  Once the malicious
// specialization is in scope, `mint_permission_split<
// tag::FederatedPeer<OrgB>, tag::FederatedPeer<OrgB>>(
// Permission<tag::FederatedPeer<OrgA>>{})` succeeds at the
// requires-clause and produces a pair of OrgB permissions from a
// single OrgA permission — total cross-org admittance escalation
// with no policy check, no federation handshake, no MAC.
//
// Defense shipped in V1:
//
//   * The partial specialization
//     `splits_into<tag::FederatedPeer<Org>,
//                  tag::FederatedPeer<A>, tag::FederatedPeer<B>>`
//     below explicitly pins federation policy as "intra-org only"
//     (`A == Org && B == Org`).  This blocks the accidental
//     same-tag-tree cross-org split: callers who DON'T ship a
//     malicious explicit specialization but reach `mint_permission_split`
//     with cross-org template arguments are rejected at the
//     requires-clause.
//   * The grep-discoverable policy manifest is now adjacent to the
//     tag declaration, satisfying the "specializations belong in
//     the SAME TU as the tags" docstring requirement structurally
//     rather than by review-only convention.
//
// Defense NOT shipped in V1 (deferred to fixy-M-29):
//
//   * A malicious EXPLICIT specialization
//     `splits_into<tag::FederatedPeer<OrgA>,
//                  tag::FederatedPeer<OrgB>, tag::FederatedPeer<OrgB>>`
//     (three concrete types) is STILL more specialized than our
//     partial and wins template-argument matching.  The structural
//     fix is to make `splits_into` namespace-private or
//     friend-constrained so that user-side specializations cannot
//     ride the public name at all.  That is fixy-M-29's scope —
//     the general `splits_into` enforcement gap.  CR-05 is the
//     federation-specific weaponization; closing it independently
//     would still leave the generic gap open for any other
//     security-critical tag tree (Cipher tier permissions, CSL
//     producer/consumer endpoints, etc.).
//
// A positive-attack regression fixture lives at
// `test/safety_attack/attack_federation_cross_org_escalation.cpp`.
// It compiles today, runs today, and asserts that a malicious
// user-side explicit specialization successfully converts one
// OrgA permission into two OrgB permissions.  When fixy-M-29 lands
// (namespace-private or friend-constrained `splits_into`), the
// malicious specialization fails to compile and this fixture's
// build itself reds — flagging that the regression must be
// rewritten as a negative-compile fixture AND that the structural
// gap behind CR-05 is closed.
//
// ── fixy-CR-06 (Hunter 5 C5) — ROOT-MINT FORGES FEDERATION ─────────
//
// `::crucible::safety::mint_permission_root<Tag>()` is a free-function
// factory.  It has no once-per-program-per-tag machinery; every TU
// can call it, and the docstring at the call site says the discipline
// "is review-only and grep-discoverable".  For ordinary tags the
// review burden is acceptable: a stray `mint_permission_root<Foo>()`
// in a random TU shows up under `grep mint_permission_root<` and a
// reviewer asks "why are you root-minting Foo?".
//
// For federation peer tags the same surface is CATASTROPHIC.  Any
// TU can write
//
//   auto perm = ::crucible::safety::mint_permission_root<
//       ::crucible::permissions::tag::FederatedPeer<AnyOrg>>();
//
// and obtain a legitimate `Permission<FederatedPeer<AnyOrg>>` —
// no admittance policy check, no handshake, no MAC, not even the
// (forgeable, fixy-CR-02) deterministic-mix64 signature path.
// Federation peer admittance — the load-bearing security boundary
// between organizations — is bypassed entirely.  CR-02/CR-03/CR-04
// at least require the attacker to call `mint_federation_admittance`
// and exploit the verifier's weaknesses; CR-06 lets the attacker
// skip the verifier altogether.
//
// Defense shipped in V1:
//
//   * The two `mint_permission_root<Tag>` overloads in
//     `::crucible::safety` (no-ctx and ctx-bound) are CONCEPT-DELETED
//     for any `Tag` matching `is_federated_peer_tag_v<Tag>`.
//     `Tag = tag::FederatedPeer<Org>` triggers concept ordering
//     (more-constrained overload wins) and the deleted definition's
//     reason string fires with a pointer to this section.
//   * Federation peer minting is routed through
//     `detail::FederationMintAccess::mint<Org>()`, a non-public helper
//     that is friended by `Permission<Tag>` and is the ONLY non-deleted
//     path to `Permission<tag::FederatedPeer<Org>>`.  The helper is
//     called exclusively from `mint_federation_admittance`, which the
//     deprecation warning + fixy-CR-02/03/04 doc-blocks already
//     audit.  Two HS14 neg-compile fixtures lock the deletion into CI
//     (`neg_fixy_federation_root_mint_disallowed{,_ctx}.cpp`).
//
// No positive-attack regression is shipped for CR-06: the structural
// gap is CLOSED today, not locked open behind a deferred milestone.
// CI verifies the closure via the neg-compile fixtures.  When the
// `Defense shipped in V1` paragraph is removed in a future audit
// (e.g., because the entire `mint_permission_root` surface becomes
// once-per-program for all tags, subsuming CR-06), the friend
// declaration in `Permission.h` and the deleted overloads here can
// be removed in the same change.

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

}  // namespace crucible::permissions

// ── fixy-CR-05 defensive splits_into manifest ──────────────────────
//
// Documents federation policy as "intra-org splits only" at the
// type level, adjacent to the federation tag tree.  Closes the
// accidental cross-org split (user has not specialized splits_into
// themselves but reaches mint_permission_split with cross-org
// template arguments).  Does NOT close the malicious explicit
// specialization — that requires fixy-M-29's namespace-private or
// friend-constrained splits_into.  See doc-block above.

namespace crucible::safety {

template <typename Org, typename A, typename B>
struct splits_into<
    ::crucible::permissions::tag::FederatedPeer<Org>,
    ::crucible::permissions::tag::FederatedPeer<A>,
    ::crucible::permissions::tag::FederatedPeer<B>>
    : std::bool_constant<std::is_same_v<A, Org>
                         && std::is_same_v<B, Org>> {};

template <typename Org, typename... Children>
struct splits_into_pack<
    ::crucible::permissions::tag::FederatedPeer<Org>,
    Children...>
    : std::bool_constant<(
        std::is_same_v<
            Children,
            ::crucible::permissions::tag::FederatedPeer<Org>> && ...)> {};

}  // namespace crucible::safety

namespace crucible::permissions {

// ── fixy-CR-06 federation-tag trait ────────────────────────────────
//
// `is_federated_peer_tag<Tag>` is the structural witness used by the
// deleted `mint_permission_root` overloads below to recognize
// federation peer tags and route minting through the admittance
// channel only.  No user code should specialize this trait — its
// match set is closed (only `tag::FederatedPeer<Org>` for some Org).

template <typename Tag>
struct is_federated_peer_tag : std::false_type {};

template <typename Org>
struct is_federated_peer_tag<tag::FederatedPeer<Org>> : std::true_type {};

template <typename Tag>
inline constexpr bool is_federated_peer_tag_v =
    is_federated_peer_tag<Tag>::value;

// ── fixy-CR-06 federation-mint chokepoint ─────────────────────────
//
// `FederationMintAccess` is the only non-deleted path to a
// `Permission<tag::FederatedPeer<Org>>`.  It is friended by
// `crucible::safety::Permission<Tag>` (see Permission.h), so it can
// invoke `Permission`'s private default constructor.  Its only
// caller in production is `mint_federation_admittance` below; any
// other call site is a CR-06 regression and is grep-discoverable
// via `FederationMintAccess::mint<`.

namespace detail {

struct FederationMintAccess {
    template <typename Org>
    [[nodiscard]] static constexpr
    ::crucible::safety::Permission<tag::FederatedPeer<Org>>
    mint() noexcept {
        return ::crucible::safety::Permission<tag::FederatedPeer<Org>>{};
    }
};

}  // namespace detail

template <typename Org>
using FederatedPeerPermission =
    ::crucible::safety::Permission<tag::FederatedPeer<Org>>;

using LocalCipherPermission =
    ::crucible::safety::Permission<tag::LocalCipherTag>;

// ── fixy-A1-008 federation strong-hash semantic types ─────────────
//
// TypeSafe axiom (CLAUDE.md §II.2) requires every semantic value
// carry a strong type.  Four uint64_t-backed handshake fields —
// org_id, peer_key_fingerprint, nonce, self_signature_fingerprint —
// are SEMANTIC and trivially swappable at positional call sites.
// `OrgId`, `PeerKeyFingerprint`, `Nonce`, `SignatureFingerprint`
// wrap them as `CRUCIBLE_STRONG_HASH`-shaped newtypes: explicit
// constructor, no implicit conversion, defaulted spaceship, no
// arithmetic — exact same machinery as `SchemaHash` etc. in
// `Types.h`.  Cross-type assignment becomes a compile error;
// param-swap on `federation_signature_fingerprint(...)` and
// `make_self_signed_handshake(...)` is closed at the type system.
//
// Layout: each is `sizeof(uint64_t) == 8`, trivially copyable; the
// four-field FederationHandshake preserves its 32-byte size and
// trivial-copyability — serialization invariants untouched.
//
// The macro `CRUCIBLE_STRONG_HASH` in Types.h is `#undef`'d at its
// end of scope by design (it's a Types.h-internal helper), so we
// expand the same shape here for the federation-domain types.

struct OrgId {
private:
    std::uint64_t v;
public:
    constexpr OrgId() noexcept : v(0) {}
    constexpr explicit OrgId(std::uint64_t val) noexcept : v(val) {}
    [[nodiscard]] static constexpr OrgId from_raw(std::uint64_t val) noexcept {
        return OrgId{val};
    }
    [[nodiscard]] constexpr std::uint64_t raw() const noexcept { return v; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return v != 0;
    }
    constexpr auto operator<=>(const OrgId&) const noexcept = default;
};
static_assert(sizeof(OrgId) == sizeof(std::uint64_t));
static_assert(std::is_trivially_copyable_v<OrgId>);

struct PeerKeyFingerprint {
private:
    std::uint64_t v;
public:
    constexpr PeerKeyFingerprint() noexcept : v(0) {}
    constexpr explicit PeerKeyFingerprint(std::uint64_t val) noexcept : v(val) {}
    [[nodiscard]] static constexpr PeerKeyFingerprint from_raw(std::uint64_t val) noexcept {
        return PeerKeyFingerprint{val};
    }
    [[nodiscard]] constexpr std::uint64_t raw() const noexcept { return v; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return v != 0;
    }
    constexpr auto operator<=>(const PeerKeyFingerprint&) const noexcept = default;
};
static_assert(sizeof(PeerKeyFingerprint) == sizeof(std::uint64_t));
static_assert(std::is_trivially_copyable_v<PeerKeyFingerprint>);

struct Nonce {
private:
    std::uint64_t v;
public:
    constexpr Nonce() noexcept : v(0) {}
    constexpr explicit Nonce(std::uint64_t val) noexcept : v(val) {}
    [[nodiscard]] static constexpr Nonce from_raw(std::uint64_t val) noexcept {
        return Nonce{val};
    }
    [[nodiscard]] constexpr std::uint64_t raw() const noexcept { return v; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return v != 0;
    }
    constexpr auto operator<=>(const Nonce&) const noexcept = default;
};
static_assert(sizeof(Nonce) == sizeof(std::uint64_t));
static_assert(std::is_trivially_copyable_v<Nonce>);

struct SignatureFingerprint {
private:
    std::uint64_t v;
public:
    constexpr SignatureFingerprint() noexcept : v(0) {}
    constexpr explicit SignatureFingerprint(std::uint64_t val) noexcept : v(val) {}
    [[nodiscard]] static constexpr SignatureFingerprint from_raw(std::uint64_t val) noexcept {
        return SignatureFingerprint{val};
    }
    [[nodiscard]] constexpr std::uint64_t raw() const noexcept { return v; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return v != 0;
    }
    constexpr auto operator<=>(const SignatureFingerprint&) const noexcept = default;
};
static_assert(sizeof(SignatureFingerprint) == sizeof(std::uint64_t));
static_assert(std::is_trivially_copyable_v<SignatureFingerprint>);

template <typename Org>
inline constexpr OrgId federation_org_id =
    OrgId{::crucible::safety::diag::stable_type_id<Org>};

namespace policy {

template <typename... Orgs>
struct admit_orgs {
    template <typename Org>
    static constexpr bool admits =
        (std::same_as<Org, Orgs> || ...);
};

}  // namespace policy

struct FederationHandshake {
    OrgId                org_id{};
    PeerKeyFingerprint   peer_key_fingerprint{};
    Nonce                nonce{};
    SignatureFingerprint self_signature_fingerprint{};
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

[[nodiscard]] constexpr SignatureFingerprint federation_signature_fingerprint(
    OrgId              org_id,
    PeerKeyFingerprint peer_key_fingerprint,
    Nonce              nonce) noexcept
{
    constexpr std::uint64_t kDomain = 0xCFED'AD11'0000'0001ULL;
    return SignatureFingerprint{
        detail::combine_runtime_ids(
            detail::combine_runtime_ids(kDomain, org_id.raw()),
            detail::combine_runtime_ids(
                peer_key_fingerprint.raw(), nonce.raw())) };
}

template <typename Org>
[[nodiscard]] constexpr PeerKeyFingerprint default_peer_key_fingerprint() noexcept {
    return PeerKeyFingerprint{
        detail::combine_runtime_ids(
            federation_org_id<Org>.raw(),
            0xCFED'9EED'0000'0001ULL) };
}

// ── fixy-L-02 #1518 — §XXI Universal Mint Pattern ──────────────────
//
// `mint_self_signed_handshake<Org>(...)` is the §XXI grep-target for
// federation handshake construction.  The handshake is the only
// path to a `FederationHandshake` POD whose `self_signature_
// fingerprint` field downstream `mint_federation_admittance` will
// accept — so the handshake IS authority-bearing material (the
// signature mechanism is forgeable per CR-02/03/04 deprecation
// notice on `mint_federation_admittance`, but renaming serves
// §XXI's audit-grep discipline regardless of the v1-production-
// readiness of the underlying crypto).
//
// Concept gate: `FederationOrgTag<Org>` — Org must be an empty
// class type, the structural shape every shipped federation org
// tag (SelfOrg / OrgPeer / OrgA / OrgBlocked / ...) actually has.
// This is the same shape `permissions::PermissionTag` uses for
// CSL permission tags and matches Source.h's tag-discipline.  The
// requirement is grep-discoverable via `FederationOrgTag<`.
//
// `make_self_signed_handshake<Org>(...)` is kept as a thin
// backwards-compat forwarder so the ~12 internal call sites in
// federation tests + safety_neg + fixy_neg fixtures don't migrate
// in lockstep with this header.  It carries NO `[[deprecated]]`
// attribute — flooding the test build with `-Wdeprecated-
// declarations` would obscure real regressions; the migration is
// a separate mechanical sweep.

template <typename Org>
concept FederationOrgTag =
    std::is_class_v<Org> && std::is_empty_v<Org>;

template <typename Org>
    requires FederationOrgTag<Org>
[[nodiscard]] constexpr FederationHandshake
mint_self_signed_handshake(
    PeerKeyFingerprint peer_key_fingerprint =
        default_peer_key_fingerprint<Org>(),
    Nonce nonce = Nonce{0}) noexcept
{
    const OrgId org_id = federation_org_id<Org>;
    return FederationHandshake{
        .org_id = org_id,
        .peer_key_fingerprint = peer_key_fingerprint,
        .nonce = nonce,
        .self_signature_fingerprint =
            federation_signature_fingerprint(
                org_id, peer_key_fingerprint, nonce),
    };
}

// Backwards-compat forwarder.  Kept WITHOUT the
// `requires FederationOrgTag<Org>` clause so the ~12 existing
// call sites in tests / safety_neg / fixy_neg / federation
// header self-test compile unchanged regardless of whether the
// caller's Org tag satisfies the stricter concept.  The §XXI
// audit grep target is `mint_self_signed_handshake<` — `make_*`
// is the legacy entry, no [[deprecated]] (emitting
// -Wdeprecated-declarations at every test call site would
// obscure real regressions during the interim before the
// mechanical migration sweep).
template <typename Org>
[[nodiscard]] constexpr FederationHandshake
make_self_signed_handshake(
    PeerKeyFingerprint peer_key_fingerprint =
        default_peer_key_fingerprint<Org>(),
    Nonce nonce = Nonce{0}) noexcept
{
    const OrgId org_id = federation_org_id<Org>;
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
    if (!handshake.peer_key_fingerprint) {
        return std::unexpected(AdmittanceError::MissingPeerKey);
    }
    if (!handshake.self_signature_fingerprint) {
        return std::unexpected(AdmittanceError::MissingSignature);
    }
    if (handshake.self_signature_fingerprint
        != federation_signature_fingerprint(
            handshake.org_id,
            handshake.peer_key_fingerprint,
            handshake.nonce)) {
        return std::unexpected(AdmittanceError::BadSignature);
    }

    return ::crucible::permissions::detail::FederationMintAccess
        ::template mint<Org>();
}

}  // namespace crucible::permissions

// ── fixy-CR-06 deleted root-mint overloads ─────────────────────────
//
// Concept-ordered `= delete` shadows the generic `mint_permission_root`
// overloads in `crucible::safety` for any federation peer tag.  The
// only legitimate path to a `Permission<tag::FederatedPeer<Org>>` is
// through `mint_federation_admittance`, which calls
// `detail::FederationMintAccess::mint<Org>()` directly.

namespace crucible::safety {

template <typename Tag>
    requires ::crucible::permissions::is_federated_peer_tag_v<Tag>
[[nodiscard]] constexpr Permission<Tag> mint_permission_root() noexcept
    = delete(
        "fixy-CR-06: Permission<tag::FederatedPeer<Org>> cannot be minted "
        "via mint_permission_root.  Federation peer admittance is the "
        "load-bearing security boundary between organizations — every "
        "minting must run the admittance policy + handshake + (once "
        "HACL* lands) MAC check.  Use "
        "`::crucible::permissions::mint_federation_admittance<Org, "
        "Policy>(local_cipher, handshake)`.  See fixy-CR-06 section in "
        "FederationPermission.h for the threat model.");

template <typename Tag, ::crucible::effects::IsExecCtx Ctx>
    requires ::crucible::permissions::is_federated_peer_tag_v<Tag>
          && CtxAdmitsPermission<Tag, Ctx>
[[nodiscard]] constexpr Permission<Tag> mint_permission_root(Ctx const&) noexcept
    = delete(
        "fixy-CR-06: Permission<tag::FederatedPeer<Org>> cannot be minted "
        "via mint_permission_root(ctx) either.  An ExecCtx argument does "
        "not authorize cross-org admittance; only "
        "`mint_federation_admittance<Org, Policy>(local_cipher, handshake)` "
        "does.  See fixy-CR-06 section in FederationPermission.h.");

}  // namespace crucible::safety

namespace crucible::permissions {

namespace detail::federation_permission_self_test {

struct SelfOrg {};
struct OtherOrg {};

static_assert(static_cast<bool>(federation_org_id<SelfOrg>));
static_assert(federation_org_id<SelfOrg> != federation_org_id<OtherOrg>);
static_assert(policy::admit_orgs<SelfOrg>::template admits<SelfOrg>);
static_assert(!policy::admit_orgs<SelfOrg>::template admits<OtherOrg>);
static_assert(policy::admit_orgs<SelfOrg, OtherOrg>::template admits<OtherOrg>);

// ── fixy-CR-05 defensive splits_into policy self-test ─────────────
//
// Confirms the partial specialization rejects cross-org and accepts
// intra-org splits.  This is the accidental-cross-org guard; a
// malicious explicit specialization still wins by being more
// specialized (see fixy-M-29 follow-up).
static_assert(::crucible::safety::splits_into_v<
              tag::FederatedPeer<SelfOrg>,
              tag::FederatedPeer<SelfOrg>,
              tag::FederatedPeer<SelfOrg>>,
              "intra-org split must be admitted");
static_assert(!::crucible::safety::splits_into_v<
              tag::FederatedPeer<SelfOrg>,
              tag::FederatedPeer<OtherOrg>,
              tag::FederatedPeer<OtherOrg>>,
              "cross-org split must be rejected by default");
static_assert(!::crucible::safety::splits_into_v<
              tag::FederatedPeer<SelfOrg>,
              tag::FederatedPeer<SelfOrg>,
              tag::FederatedPeer<OtherOrg>>,
              "mixed-org split (one child crosses) must be rejected");
static_assert(::crucible::safety::splits_into_pack_v<
              tag::FederatedPeer<SelfOrg>,
              tag::FederatedPeer<SelfOrg>,
              tag::FederatedPeer<SelfOrg>,
              tag::FederatedPeer<SelfOrg>>,
              "intra-org N-ary split must be admitted");
static_assert(!::crucible::safety::splits_into_pack_v<
              tag::FederatedPeer<SelfOrg>,
              tag::FederatedPeer<SelfOrg>,
              tag::FederatedPeer<OtherOrg>>,
              "N-ary split with one cross-org child must be rejected");

static_assert(std::is_same_v<
    FederatedPeerPermission<SelfOrg>::tag_type,
    tag::FederatedPeer<SelfOrg>>);

constexpr FederationHandshake kSelfHandshake =
    make_self_signed_handshake<SelfOrg>(
        PeerKeyFingerprint{123}, Nonce{456});
static_assert(kSelfHandshake.org_id == federation_org_id<SelfOrg>);
static_assert(kSelfHandshake.peer_key_fingerprint == PeerKeyFingerprint{123});
static_assert(kSelfHandshake.self_signature_fingerprint
              == federation_signature_fingerprint(
                  federation_org_id<SelfOrg>,
                  PeerKeyFingerprint{123},
                  Nonce{456}));

}  // namespace detail::federation_permission_self_test

}  // namespace crucible::permissions
