// safety/source/Path.h — fine-grained path-provenance taint tags.
//
// FIXY-V-232 (Agent 9 §4.4).  Three tag types are added as siblings
// of the inaugural `source::External` provenance tag introduced by
// V-031 + the broader `safety::source::*` catalog in Tagged.h:
//
//   source::FromUserPath   — raw user-typed path from CLI args /
//                            REPL / stdin.  Most adversarial input
//                            class: the operator may intentionally
//                            try to escape the sandbox.
//   source::FromEnvPath    — path string read from getenv("CRUCIBLE_*")
//                            or similar.  Less adversarial than CLI
//                            within a CI runner, but still external:
//                            the env may be inherited from a parent
//                            process the operator doesn't control.
//   source::FromConfigPath — path from JSON / YAML / TOML config
//                            file authored by the operator.  Often
//                            structurally validated by the config
//                            parser before reaching Crucible, but
//                            the bytes themselves are still external.
//
// ── Why three tags, not one (External) ─────────────────────────────
//
// Today the existing `safety::source::External` lumps all three of
// these provenance lanes together.  That is correct for any logic
// that just wants "did this come from outside Crucible's address
// space?", but it makes per-source policy hard to express:
//
//   * A FromUserPath might warrant ABSOLUTE_ROOT_LOCKED sanitization
//     (refuse any path that doesn't sit under a pre-declared safe
//     root) because REPL users are not constrained by config schema.
//   * A FromEnvPath may carry deployment-specific implicit trust
//     within a CI runner ("`CRUCIBLE_DATA_ROOT` is set by the
//     orchestrator, not by the workload") — the appropriate
//     sanitization is weaker than CLI's.
//   * A FromConfigPath was already through a structured-parser
//     validation step; the sanitization step can rely on that
//     prior validation as a precondition.
//
// V-233 (safety/sanitize/PathTraversal.h) ships the per-source
// sanitize-policy overloads.  V-232 (this file) ships ONLY the tag
// types + the V-023 retag_policy admittances that route each
// narrower tag forward into `source::Sanitized` once the appropriate
// V-233 sanitize policy has run.
//
// ── Relationship to source::External ───────────────────────────────
//
// FromUserPath / FromEnvPath / FromConfigPath are ORTHOGONAL
// narrowings, NOT subtypes-of-External in the type system.  All
// four are sibling phantom tags on the same `source::` axis.  The
// type-system relationships across the four:
//
//   FromUserPath   → External:        REJECTED.  Widening would
//                                     erase the "this came from CLI
//                                     specifically" audit trail.
//                                     Production code should never
//                                     need this transition.
//   External       → FromUserPath:    REJECTED.  Re-tightening would
//                                     be CLAIMING a narrower
//                                     provenance retroactively for
//                                     arbitrary External bytes.
//                                     That's lying about origin
//                                     and defeats per-source policy.
//   FromUserPath   → FromEnvPath:     REJECTED.  Cross-narrowing.
//                                     If a value came from CLI it
//                                     never came from env.
//   FromUserPath   → Sanitized:       ADMITTED via V-232 catalog.
//                                     V-233 will ship the sanitize
//                                     policy that actually performs
//                                     the discharge.
//
// The inverse — Sanitized back to any narrower tag — stays REJECTED
// because re-introducing taint is the same anti-pattern V-023
// already documents at Tagged.h:712.
//
// ── Why a separate file (safety/source/Path.h) ─────────────────────
//
// `safety/Tagged.h` is already 750+ lines; the source:: catalog
// alone is ~75 lines.  V-232 + V-233 + V-234 collectively add ~10
// more tag types and ~15 retag_policy entries directly related to
// path-provenance.  Carving out a `safety/source/` subdirectory:
//
//   * Keeps Tagged.h focused on the wrapper machinery + the
//     primary policy catalog.
//   * Gives the path-provenance lane a single grep target as it
//     grows (V-233 adds policies; V-234 adds release_aware<>
//     composition; future fleet-deployment scenarios may add e.g.
//     source::FromKubeSecret).
//   * Stays inside the canonical `crucible::safety::source`
//     namespace so all the existing fixy::tags::source aliases
//     and retag_policy<> consumers see the new tags through
//     standard name lookup; no new fixy plumbing needed.
//
// The substrate (`safety::source::*`) is intentionally NOT split
// into nested namespaces per file — every tag lives in the flat
// `safety::source::` namespace so the type-name surface stays
// uniform (`source::FromUser`, `source::FromUserPath`,
// `source::Sanitized` all sit at the same depth).
//
// ── Axiom coverage ────────────────────────────────────────────────
//
//   InitSafe   — Tags are empty structs; default-construction is
//                trivial.  Each Tagged<T, source::FromXxxPath>
//                inherits the initialization discipline of T plus
//                the lattice's empty-element grade.
//   TypeSafe   — Three new distinct types.  Passing
//                Tagged<path, FromUserPath> where FromEnvPath is
//                expected is a compile error.
//   NullSafe   — Tags have no pointer state.
//   MemSafe    — Tags are POD, zero size, zero lifetime concerns.
//   BorrowSafe — `RetagAllowed<>` concept rejects
//                cross-lane retag at compile time (V-024 wiring).
//   ThreadSafe — Tag identity is compile-time only; no runtime
//                state, no atomic ordering.
//   LeakSafe   — Empty structs have no destructors to leak.
//   DetSafe    — Tag identity is byte-deterministic across builds,
//                platforms, and Crucible versions (within compile-
//                version window).  The retag transition produces no
//                runtime artifact — purely phantom.
//
// ── Cost ──────────────────────────────────────────────────────────
//
// Zero.  Empty struct definitions emit no symbols; retag_policy<>
// specializations are constexpr `static constexpr bool allowed`
// values folded at compile time; the in-header self-test block is
// static_assert-only (no runtime instructions).

#pragma once

#include <crucible/safety/Tagged.h>  // safety::source::*, retag_policy, RetagAllowed

namespace crucible::safety::source {

// ── source::FromUserPath ───────────────────────────────────────────
//
// Provenance tag for paths originating in user-supplied data: CLI
// flags (`argv[N]`), REPL input, ad-hoc command-line strings.  The
// most adversarial input class — operators may construct paths
// specifically designed to escape the intended sandbox.  Sanitize
// policy (V-233) must apply the full predicate stack: no_dotdot,
// no_embedded_nul, no_oversize, absolute_root_locked (where
// applicable).
struct FromUserPath   {};

// ── source::FromEnvPath ────────────────────────────────────────────
//
// Provenance tag for paths read from environment variables
// (`getenv("CRUCIBLE_*")`, etc.).  Less adversarial than
// FromUserPath in a controlled CI / orchestrator deployment, where
// the env is set by the deployment harness, not by an interactive
// user.  Still external — the env may be inherited from a parent
// process Crucible doesn't own.  Sanitize policy may relax
// absolute_root_locked when the deployment manifest pre-declares
// the env value's expected root.
struct FromEnvPath    {};

// ── source::FromConfigPath ─────────────────────────────────────────
//
// Provenance tag for paths drawn from operator-supplied
// configuration files (JSON / YAML / TOML).  Typically pre-validated
// by the structured parser (schema check, type check) before
// reaching Crucible, so the sanitize policy can rely on the parser
// already having rejected obviously-broken inputs.  Still external
// — the config file itself is operator-authored.
struct FromConfigPath {};

}  // namespace crucible::safety::source

namespace crucible::safety {

// ── retag_policy<FromXxxPath, Sanitized> — V-232 catalog ───────────
//
// Each narrower path-provenance tag launders forward into
// `source::Sanitized` once the appropriate V-233 sanitize policy has
// run.  The substrate primary template (V-022, Tagged.h:410) stays
// fail-closed; this file ships ONLY the three forward admittances
// V-232 needs.  Inverse directions (Sanitized → narrower path) stay
// rejected by the fail-closed primary — re-introducing taint is the
// same anti-pattern V-023 catalog rejects at Tagged.h:712.
//
// Cross-narrowing transitions (FromUserPath → FromEnvPath, etc.)
// are NOT admitted: a value that came from CLI cannot subsequently
// be relabeled as coming from env without re-running the sanitize
// boundary.  The fail-closed primary handles those automatically.

template <> struct retag_policy<source::FromUserPath,   source::Sanitized> {
    // Discharge: V-233's sanitize_path<FromUserPath> overload ran
    // and the path passed the full predicate stack.
    static constexpr bool allowed = true;
};

template <> struct retag_policy<source::FromEnvPath,    source::Sanitized> {
    // Discharge: V-233's sanitize_path<FromEnvPath> overload ran
    // and the env-sourced path passed the deployment-scoped
    // sanitize predicate.
    static constexpr bool allowed = true;
};

template <> struct retag_policy<source::FromConfigPath, source::Sanitized> {
    // Discharge: V-233's sanitize_path<FromConfigPath> overload
    // ran and the config-sourced path passed the parser-scoped
    // sanitize predicate.
    static constexpr bool allowed = true;
};

}  // namespace crucible::safety

// ── V-232 self-test — pin the catalog at sentinel-TU compile ───────
//
// Witness the three load-bearing properties of V-232:
//   1. The three new tags are distinct types (TypeSafe);
//   2. Each forward admittance into Sanitized is reachable through
//      both the policy table AND the V-024 concept;
//   3. The inverse directions stay rejected by the fail-closed
//      primary template.
// If any of these flip, the build breaks HERE rather than at 40
// downstream consumer sites.

namespace crucible::safety::source::detail::v232_self_test {

// ── (1) Tag distinctness ───────────────────────────────────────────
static_assert(!std::is_same_v<FromUserPath, FromEnvPath>,
    "FIXY-V-232: FromUserPath and FromEnvPath must be distinct types "
    "(different provenance lanes carry different per-source policy).");
static_assert(!std::is_same_v<FromUserPath, FromConfigPath>,
    "FIXY-V-232: FromUserPath and FromConfigPath must be distinct types.");
static_assert(!std::is_same_v<FromEnvPath,  FromConfigPath>,
    "FIXY-V-232: FromEnvPath and FromConfigPath must be distinct types.");

// The three new tags must also be distinct from the existing
// source::External — they are SIBLINGS of External, not aliases.
static_assert(!std::is_same_v<FromUserPath,   External>,
    "FIXY-V-232: FromUserPath must be a distinct type from External "
    "— widening would erase the audit trail of where the path came from.");
static_assert(!std::is_same_v<FromEnvPath,    External>,
    "FIXY-V-232: FromEnvPath must be a distinct type from External.");
static_assert(!std::is_same_v<FromConfigPath, External>,
    "FIXY-V-232: FromConfigPath must be a distinct type from External.");

// ── (2) Forward admittance reachable through policy + concept ──────
static_assert(retag_policy<FromUserPath,   Sanitized>::allowed,
    "FIXY-V-232: source::FromUserPath → source::Sanitized must be "
    "admitted by the V-232 retag_policy catalog so V-233's "
    "sanitize_path<FromUserPath> overload can discharge.");
static_assert(retag_policy<FromEnvPath,    Sanitized>::allowed,
    "FIXY-V-232: source::FromEnvPath → source::Sanitized must be "
    "admitted by the V-232 retag_policy catalog.");
static_assert(retag_policy<FromConfigPath, Sanitized>::allowed,
    "FIXY-V-232: source::FromConfigPath → source::Sanitized must be "
    "admitted by the V-232 retag_policy catalog.");

// Same reach via the V-024 RetagAllowed concept — pins that the
// using-declaration / concept form sees the new specializations.
static_assert(RetagAllowed<FromUserPath,   Sanitized>,
    "FIXY-V-232: RetagAllowed concept must admit FromUserPath → "
    "Sanitized via the V-232 catalog through the V-024 wire-up.");
static_assert(RetagAllowed<FromEnvPath,    Sanitized>,
    "FIXY-V-232: RetagAllowed concept must admit FromEnvPath → "
    "Sanitized.");
static_assert(RetagAllowed<FromConfigPath, Sanitized>,
    "FIXY-V-232: RetagAllowed concept must admit FromConfigPath → "
    "Sanitized.");

// V-023's pre-existing External → Sanitized admittance must still
// reach — V-232 does not collide with the catalog already shipped.
static_assert(retag_policy<External, Sanitized>::allowed,
    "FIXY-V-232: V-023's source::External → source::Sanitized "
    "must remain admitted after V-232 catalog additions.");

// ── (3) Inverse rejection — fail-closed for narrowing-from-Sanitized
//
// Once a path is Sanitized, retagging back to any narrower
// provenance is a taint-reintroduction; reject (no specialization
// shipped for any of these directions, so the V-022 primary
// template's fail-closed default fires).
static_assert(!retag_policy<Sanitized, FromUserPath>::allowed,
    "FIXY-V-232: Sanitized → FromUserPath must stay rejected — "
    "re-introducing taint defeats the sanitize boundary.");
static_assert(!retag_policy<Sanitized, FromEnvPath>::allowed,
    "FIXY-V-232: Sanitized → FromEnvPath must stay rejected.");
static_assert(!retag_policy<Sanitized, FromConfigPath>::allowed,
    "FIXY-V-232: Sanitized → FromConfigPath must stay rejected.");

// ── (3b) Inverse rejection — fail-closed for cross-narrowing ───────
//
// A value that came from CLI cannot subsequently be relabeled as
// coming from env (or config) — provenance lanes are orthogonal.
// The V-022 fail-closed primary handles all six cross-narrowing
// pairs; witness three representative ones.
static_assert(!retag_policy<FromUserPath, FromEnvPath>::allowed,
    "FIXY-V-232: FromUserPath → FromEnvPath must stay rejected — "
    "provenance lanes are orthogonal, cross-narrowing would lie "
    "about origin.");
static_assert(!retag_policy<FromEnvPath,  FromConfigPath>::allowed,
    "FIXY-V-232: FromEnvPath → FromConfigPath must stay rejected.");
static_assert(!retag_policy<FromConfigPath, FromUserPath>::allowed,
    "FIXY-V-232: FromConfigPath → FromUserPath must stay rejected.");

// ── (3c) External re-tightening rejection ──────────────────────────
//
// The existing External tag is wider than any of the three new
// narrowings.  Going wide → narrow is BACK-FILLING provenance
// (claiming a narrower lineage retroactively); reject in both
// directions for the same reason as (3b).
static_assert(!retag_policy<External, FromUserPath>::allowed,
    "FIXY-V-232: External → FromUserPath must stay rejected — "
    "back-filling narrower provenance retroactively is a lie about "
    "the audit trail.");
static_assert(!retag_policy<External, FromEnvPath>::allowed,
    "FIXY-V-232: External → FromEnvPath must stay rejected.");
static_assert(!retag_policy<External, FromConfigPath>::allowed,
    "FIXY-V-232: External → FromConfigPath must stay rejected.");

// Narrowing → External (widening) also stays rejected — the
// narrower audit trail must not be erased upward.
static_assert(!retag_policy<FromUserPath,   External>::allowed,
    "FIXY-V-232: FromUserPath → External must stay rejected — "
    "widening would erase the narrower audit trail.");
static_assert(!retag_policy<FromEnvPath,    External>::allowed,
    "FIXY-V-232: FromEnvPath → External must stay rejected.");
static_assert(!retag_policy<FromConfigPath, External>::allowed,
    "FIXY-V-232: FromConfigPath → External must stay rejected.");

// ── (4) Identity admitted by V-022's identity specialization ───────
//
// (X → X) is always admitted unconditionally per V-022's
// `retag_policy<Tag, Tag>` specialization at Tagged.h:423.  Witness
// for each new tag — pins that the V-022 identity rule survives
// the V-232 catalog additions.
static_assert(retag_policy<FromUserPath,   FromUserPath>::allowed,
    "FIXY-V-232: V-022 identity (FromUserPath → FromUserPath) must "
    "stay admitted after the V-232 catalog additions.");
static_assert(retag_policy<FromEnvPath,    FromEnvPath>::allowed,
    "FIXY-V-232: V-022 identity (FromEnvPath → FromEnvPath) admitted.");
static_assert(retag_policy<FromConfigPath, FromConfigPath>::allowed,
    "FIXY-V-232: V-022 identity (FromConfigPath → FromConfigPath) admitted.");

}  // namespace crucible::safety::source::detail::v232_self_test
