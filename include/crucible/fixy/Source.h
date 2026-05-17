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

}  // namespace crucible::fixy::tags::self_test
