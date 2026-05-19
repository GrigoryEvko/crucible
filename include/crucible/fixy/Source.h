#pragma once

// ── crucible::fixy::tags — Source/Trust/Access/Version/Secret/Hash ──
//
// Phase D re-export per misc/16_05_2026_fixy.md.  Surfaces every
// load-bearing PROVENANCE / TRUST / ACCESS / VERSION / SECRET-POLICY /
// HASH-FAMILY tag tree under `fixy::tags::<axis>::` so callers who
// include only the fixy umbrella never have to descend into the
// safety/ tree to phantom-tag a value.
//
// Per CLAUDE.md §XXI Universal Mint Pattern: these are NOT minters —
// they are pure phantom-type re-exports.  Tag namespaces are
// inline-namespace aliased; no new types are introduced; every tag
// `fixy::tags::source::FromUser` IS `safety::source::FromUser`
// (same address, same vtable, same friend access).
//
// ── Substrate consumed ─────────────────────────────────────────────
//
//   safety/Tagged.h
//     namespace safety::source         (60+ provenance tags)
//     namespace safety::trust          (5 verification tags)
//     namespace safety::access         (10 access-mode tags)
//     namespace safety::version        (V<N> template)
//     namespace safety::vessel_trust   (FromPytorch / Validated)
//   safety/Secret.h
//     namespace safety::secret_policy  (5 declassify policies)
//   Types.h
//     namespace hash_family            (FamilyA / FamilyB)
//   permissions/FederationPermission.h
//     extends safety::source::*        (federation-only tags)
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe — tags are empty structs; default-construction is trivial.
//   TypeSafe — namespace aliases preserve typename identity; every
//              re-exported tag IS the substrate tag (same definition,
//              not a re-declaration).  Mismatch across axes (passing
//              source::FromUser where trust::Verified expected) fires
//              the substrate's overload-resolution diagnostic.
//   NullSafe — tags have no pointer state.
//   MemSafe  — tags are POD, zero size, zero lifetime concerns.
//   DetSafe  — tag identity is compile-time only; never appears in a
//              runtime data path; bit-exact across re-export.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  `namespace source = ::crucible::safety::source;` is a name-
// lookup directive only; no symbols emitted, no types introduced.

#include <crucible/Types.h>                        // hash_family::*
#include <crucible/permissions/FederationPermission.h>  // federation::*
#include <crucible/safety/Secret.h>                // secret_policy::*
#include <crucible/safety/Tagged.h>                // source::*, trust::*, access::*, version::*, vessel_trust::*

namespace crucible::fixy::tags {

// ── source::* — provenance tagging (~60 tags) ──────────────────────
//
// Every cross-trust-boundary value (Vessel input, Cipher load, network
// payload, FFI return, kernel telemetry, registry row, vendor truth,
// SDC verification result, ...) carries a `source::*` tag through the
// type system until it is explicitly retagged to a sanitized class.
namespace source = ::crucible::safety::source;

// ── trust::* — verification status (5 tags) ────────────────────────
//
// Orthogonal axis to source::: WHERE a value came from (source) vs HOW
// confident we are it satisfies its invariant (trust).  Verified /
// Tested / Unverified / Assumed / External.
namespace trust = ::crucible::safety::trust;

// ── access::* — access-mode tagging (10 tags) ──────────────────────
//
// Register / column / field semantics: RW, RO, WO, W1C, W1S, WriteOnce,
// AppendOnly, Unique, AutoIncrement, Deprecated.  Composes orthogonally
// with source:: + trust::.
namespace access = ::crucible::safety::access;

// ── version::* — schema versioning (V<N>) ──────────────────────────
//
// Compile-time schema-version phantom — `Tagged<T, version::V<3>>`
// rejects passing across an interface that expects `V<2>`.
namespace version = ::crucible::safety::version;

// ── vessel_trust::* — Vessel-boundary trust (2 tags) ───────────────
//
// Raw Vessel input arrives as `FromPytorch`; Vessel-side validators
// retag to `Validated` after well-formedness checks pass.  Internal
// paths require `Validated` at entry; the type system rejects the
// `FromPytorch → record` shortcut.
namespace vessel_trust = ::crucible::safety::vessel_trust;

// ── secret_policy::* — Secret<T> declassification policies (5) ─────
//
// `secret.declassify<secret_policy::AuditedLogging>(...)` etc.  Each
// policy tag carries an audit obligation grep-discoverable by name.
namespace secret_policy = ::crucible::safety::secret_policy;

// ── hash_family::* — persistence semantics of hashes (2) ───────────
//
// Family A = persistent across (process × platform × Crucible version
// within compile_version window) for Cipher entry keys.  Family B =
// process-local, may differ across runs.  Tagging at field declaration
// time discriminates the persistence contract.
namespace hash_family = ::crucible::hash_family;

}  // namespace crucible::fixy::tags

// ═════════════════════════════════════════════════════════════════════
// ── Federation re-export (FIXY-AUDIT-C9) ───────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// `permissions/FederationPermission.h` is the cross-organization
// admission boundary for Cipher federation.  It carries:
//
//   safety::source::FederatedPeer<Org>          — provenance tag
//   permissions::tag::FederatedPeer<Org>        — permission tag
//   permissions::tag::LocalCipherTag            — local-authority tag
//   permissions::FederatedPeerPermission<Org>   — Permission<tag::FederatedPeer<Org>>
//   permissions::LocalCipherPermission          — Permission<tag::LocalCipherTag>
//   permissions::federation_org_id<Org>         — stable type id
//   permissions::policy::admit_orgs<Orgs...>    — admission policy
//   permissions::FederationHandshake            — handshake POD
//   permissions::AdmittanceError                — error enum + name()
//   permissions::federation_signature_fingerprint(...)
//   permissions::default_peer_key_fingerprint<Org>()
//   permissions::make_self_signed_handshake<Org>(...)
//   permissions::mint_federation_admittance<Org, Policy>(local, hs)
//
// Per CLAUDE.md §XXI Universal Mint Pattern: `mint_federation_admittance`
// is a token mint — derives FederatedPeerPermission<Org> authority from
// a LocalCipherPermission + a verified handshake.  The re-export
// preserves the substrate's std::expected error path and the
// AdmittanceError surface.
//
// `safety::source::FederatedPeer<Org>` is the PROVENANCE tag (extends
// the source axis); it lives in `crucible::safety::source` and is
// therefore already reachable via the `fixy::tags::source` namespace
// alias above as `fixy::tags::source::FederatedPeer<Org>`.
//
// ── Disambiguation (fixy-A4-013) ──────────────────────────────────
//
// TWO distinct types named `FederatedPeer<Org>` are reachable through
// the fixy umbrella.  They carry the SAME identifier name but live on
// ORTHOGONAL axes (Bell-LaPadula 1973 information-flow vs CSL frame
// rule), so the collision is structural — not a typo, not a re-export
// bug:
//
//   fixy::tags::source::FederatedPeer<Org>
//     ≡ ::crucible::safety::source::FederatedPeer<Org>
//     Axis: PROVENANCE (the "who said this value")
//     Used in: Tagged<T, source::FederatedPeer<Org>> on cross-org
//              Cipher payloads.  Compile-time only; never appears in
//              a Permission carrier.
//
//   fixy::source::federation::FederatedPeer<Org>
//     ≡ ::crucible::permissions::tag::FederatedPeer<Org>
//     Axis: PERMISSION (the "who is allowed to act on this value")
//     Used in: Permission<tag::FederatedPeer<Org>> → carrier
//              FederatedPeerPermission<Org>.  Runtime move-only token
//              under the CSL linearity discipline.
//
// A caller TU that does BOTH `using namespace fixy::tags::source;` and
// `using namespace fixy::source::federation;` will see two
// declarations of `FederatedPeer<Org>` in the same lookup scope; bare
// `FederatedPeer<MyOrg> x;` is ambiguous and the compiler rejects.
// This is correct behavior — the user must qualify
// (`safety::source::FederatedPeer<MyOrg>` vs
// `permissions::tag::FederatedPeer<MyOrg>`) so the AXIS choice is
// visible at the use site.  The disambiguation static_assert in
// `fixy::tags::self_test` (search "fixy-A4-013") pins the
// "two-distinct-types" fact at compile time; if a future refactor
// makes the two types alias to the same substrate (e.g. by promoting
// one axis to the other), the build breaks here.

namespace crucible::fixy::source::federation {

// ── Permission tags (per-org typed admission) ──────────────────────

using ::crucible::permissions::tag::FederatedPeer;
using ::crucible::permissions::tag::LocalCipherTag;

// ── Permission carriers ────────────────────────────────────────────

using ::crucible::permissions::FederatedPeerPermission;
using ::crucible::permissions::LocalCipherPermission;

// ── Identity / policy ──────────────────────────────────────────────

using ::crucible::permissions::federation_org_id;

// ── fixy-A1-008 strong-hash semantic types ─────────────────────────
//
// `OrgId` / `PeerKeyFingerprint` / `Nonce` / `SignatureFingerprint`
// are the TypeSafe newtypes that replace the bare uint64_t fields of
// `FederationHandshake`.  Re-exported here so fixy users construct
// handshakes without leaving the `fixy::source::federation`
// namespace.

using ::crucible::permissions::OrgId;
using ::crucible::permissions::PeerKeyFingerprint;
using ::crucible::permissions::Nonce;
using ::crucible::permissions::SignatureFingerprint;

namespace policy {
using ::crucible::permissions::policy::admit_orgs;
}  // namespace policy

// ── Handshake POD + error surface ──────────────────────────────────

using ::crucible::permissions::FederationHandshake;
using ::crucible::permissions::AdmittanceError;
using ::crucible::permissions::admittance_error_name;

// ── Signature derivation + helpers ─────────────────────────────────

using ::crucible::permissions::federation_signature_fingerprint;
using ::crucible::permissions::default_peer_key_fingerprint;
using ::crucible::permissions::make_self_signed_handshake;

// ── Token mint — Universal Mint Pattern (CLAUDE.md §XXI) ───────────

using ::crucible::permissions::mint_federation_admittance;

}  // namespace crucible::fixy::source::federation

// ── Self-test ──────────────────────────────────────────────────────
//
// Witness that namespace aliases preserve typename identity.  If a
// substrate tag is renamed, deleted, or moved out from under us, the
// `std::is_same_v` check fires at sentinel-TU compile.

namespace crucible::fixy::tags::self_test {

// One canonical witness per axis — full coverage in test_fixy_source.cpp.
static_assert(std::is_same_v<source::FromUser,
                             ::crucible::safety::source::FromUser>,
    "fixy::tags::source::FromUser must alias safety::source::FromUser");

static_assert(std::is_same_v<trust::Verified,
                             ::crucible::safety::trust::Verified>,
    "fixy::tags::trust::Verified must alias safety::trust::Verified");

static_assert(std::is_same_v<access::RW,
                             ::crucible::safety::access::RW>,
    "fixy::tags::access::RW must alias safety::access::RW");

static_assert(std::is_same_v<version::V<3>,
                             ::crucible::safety::version::V<3>>,
    "fixy::tags::version::V<N> must alias safety::version::V<N>");

static_assert(std::is_same_v<vessel_trust::Validated,
                             ::crucible::safety::vessel_trust::Validated>,
    "fixy::tags::vessel_trust::Validated must alias safety::vessel_trust::Validated");

static_assert(std::is_same_v<secret_policy::AuditedLogging,
                             ::crucible::safety::secret_policy::AuditedLogging>,
    "fixy::tags::secret_policy::AuditedLogging must alias the substrate tag");

static_assert(std::is_same_v<hash_family::FamilyA,
                             ::crucible::hash_family::FamilyA>,
    "fixy::tags::hash_family::FamilyA must alias hash_family::FamilyA");

// ── fixy-A4-013: provenance / permission FederatedPeer disambiguation
//
// Two distinct substrate types share the identifier `FederatedPeer<Org>`,
// reachable through the umbrella on different namespace paths.  Pin
// the distinction at compile time so a future refactor that would
// silently unify them (e.g. making the provenance tag inherit from
// the permission tag) breaks the build here, BEFORE it propagates
// into Tagged<>/Permission<> call sites where the diagnostic would
// be far less localized.
//
// Witness with a phantom Org parameter — federation_org_id<Org> is
// the only structural requirement and ints satisfy it as well as a
// concrete Org tag struct.
namespace a4_013_disambiguation {
struct ProbeOrg {};
}  // namespace a4_013_disambiguation

static_assert(!std::is_same_v<
        ::crucible::fixy::tags::source::FederatedPeer<a4_013_disambiguation::ProbeOrg>,
        ::crucible::fixy::source::federation::FederatedPeer<a4_013_disambiguation::ProbeOrg>>,
    "fixy-A4-013: fixy::tags::source::FederatedPeer (provenance axis) and "
    "fixy::source::federation::FederatedPeer (permission axis) must remain "
    "distinct substrate types.  Unification would collapse two orthogonal "
    "axes (Bell-LaPadula provenance vs CSL frame-rule permission) into one "
    "and break the umbrella's axis-discrimination contract.");

// Symmetric positive assertions — pin each fixy path to its substrate
// origin so a rename on either substrate side reddens HERE rather
// than 40 call sites downstream.
static_assert(std::is_same_v<
        ::crucible::fixy::tags::source::FederatedPeer<a4_013_disambiguation::ProbeOrg>,
        ::crucible::safety::source::FederatedPeer<a4_013_disambiguation::ProbeOrg>>,
    "fixy-A4-013: fixy::tags::source::FederatedPeer must alias "
    "safety::source::FederatedPeer (provenance axis).");

static_assert(std::is_same_v<
        ::crucible::fixy::source::federation::FederatedPeer<a4_013_disambiguation::ProbeOrg>,
        ::crucible::permissions::tag::FederatedPeer<a4_013_disambiguation::ProbeOrg>>,
    "fixy-A4-013: fixy::source::federation::FederatedPeer must alias "
    "permissions::tag::FederatedPeer (permission axis).");

}  // namespace crucible::fixy::tags::self_test
