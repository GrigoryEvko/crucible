#pragma once

// ── crucible::fixy::grant — engagement markers + relaxation tags ───
//
// Clean reimplementation per misc/16_05_2026_fixy.md §4.
//
// Every fixy:: binding's `Grants` pack consists of tags from this
// header.  Each tag:
//
//   (1) inherits `grant_base` (structural marker for IsGrantTag)
//   (2) is `final` (per safety/NotInherited.h discipline — caller may
//       not extend tags to inject behavior into IsAccepted)
//   (3) names the dimension it engages with via `which_dim_v<G>` —
//       used by Reject.h to compute the engagement check
//   (4) is empty (zero-state) — every tag is EBO-collapsible
//
// Two grant flavors:
//
//   ── Acceptance markers ─────────────────────────────────────────
//   `accept_default_strict_for<D>` — the author has read the
//   discipline and accepts the strict default on dim D.  These tags
//   carry no payload; they are pure engagement markers.  Required
//   for every dim that doesn't have an explicit relaxation in the
//   `Grants` pack.
//
//   ── Relaxation tags ────────────────────────────────────────────
//   `affine`, `copy`, `ghost`, `with<Es...>`, `declassify<Policy>`,
//   `vendor_backend<V>`, etc.  Each relaxation tag carries the
//   per-axis information needed to resolve the underlying `Fn<...>`
//   instantiation in `fixy/Fn.h`'s `detail::resolve` namespace.
//   IsAccepted only needs the tag's identity (which dim it engages
//   with); the resolution semantics live in `Fn.h`'s resolver.
//
// ── Substrate consumed ─────────────────────────────────────────────
//
//   safety::NotInherited / FinalBy — final-class discipline
//   safety::source::* / trust::*    — Provenance / Trust aliases
//   safety::fn::pred::True          — Refinement strict default
//   effects::Row / effects::Effect  — Effect pack
//
// ── Substrate added by this header ─────────────────────────────────
//
// NONE.  Every relaxation tag is a phantom type carrying parameters
// that downstream consumers (`fixy/Fn.h`'s `detail::resolve`
// namespace) project onto substrate template arguments.  No new
// lattice, no new wrapper.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  Every tag is empty + final + inherits a tag-base; `sizeof`
// is 1, EBO-collapse takes that to 0 in any aggregator.
//
// ── Self-test ──────────────────────────────────────────────────────
//
// Three load-bearing assertions:
//   1. Every shipped grant tag inherits `grant_base`.
//   2. Every shipped grant tag is `final`.
//   3. Every DimensionAxis has at least one acceptance marker (the
//      `accept_default_strict_for<D>` specialization), enforced by
//      the reflection-driven coverage check in Reject.h.

#include <crucible/fixy/Dim.h>
#include <crucible/safety/NotInherited.h>
#include <crucible/safety/Secret.h>
#include <crucible/safety/Tagged.h>
#include <crucible/safety/Fn.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

#include <concepts>
#include <cstdint>
#include <type_traits>

namespace crucible::fixy::grant {

// ─── grant_base — structural marker for every grant tag ────────────
//
// Detection is `is_base_of_v<grant_base, T>` (no trait specialization
// gymnastics).  Every shipped grant tag inherits this; user-defined
// tags that want to participate in `IsAccepted` MUST inherit too —
// the cheat-probe round will catch a tag that doesn't.

struct grant_base {
    constexpr grant_base()                              noexcept = default;
    constexpr grant_base(const grant_base&)             noexcept = default;
    constexpr grant_base(grant_base&&)                  noexcept = default;
    constexpr grant_base& operator=(const grant_base&)  noexcept = default;
    constexpr grant_base& operator=(grant_base&&)       noexcept = default;
    ~grant_base()                                                = default;
};

// IsGrantTag — structural concept gate.
//
// Three clauses: (a) G is its own cv-ref-stripped form — no
// `const`/`volatile`/reference qualifiers; (b) inherits grant_base;
// (c) is final per safety/NotInherited.h discipline.  Inheriting
// without final would allow a user-defined subclass to inject
// behavior into IsAccepted by extending an already-shipped grant
// tag; the cheat-probe round caches a neg-compile fixture for this
// exact attempt.
//
// ── cv-ref rejection (fixy-A4-033) ─────────────────────────────────
//
// Grant tags are pure zero-state phantom markers — every shipped
// tag is empty + final + 1-byte sized.  There is no legitimate code
// path that produces a cv-qualified or reference-qualified grant
// type; such a type arises only from copy-paste of a `decltype` on
// a runtime variable (`const auto g = affine{}; decltype(g)` →
// `const affine`) or a reference return.  The pre-A4-033 form
// stripped cv-ref via `std::remove_cvref_t` before checking, which
// silently accepted these copy-paste mistakes; the tightened form
// requires the caller to spell the bare grant type.
//
// Effect on diagnostic surface: a cv-ref-qualified grant now flows
// through the same `AllGrantsWellFormed` rejection path as a
// non-grant type (e.g. `int` in the pack), instead of being
// silently coerced to its cv-ref-stripped form and projected onto
// the resolver.  See test/fixy_neg/neg_fixy_grant_cv_qualified.cpp
// and test/fixy_neg/neg_fixy_grant_reference.cpp.
//
// ── Defense-surface honesty (fixy-CR-09) ───────────────────────────
//
// The structural recipe — `final` + inherits `grant_base` + has a
// `which_dim` specialization — is replicable by any user who:
//
//   (1) opens `namespace crucible::fixy::grant { ... }`,
//   (2) defines a `final` struct deriving `grant_base`,
//   (3) specializes `which_dim<their_type>` from inside the reopened
//       namespace.
//
// The C++ language has no namespace-scoped specialization access
// control: explicit template specializations of `which_dim` MUST
// live syntactically inside `crucible::fixy::grant`, and a user that
// reopens the namespace can register a foreign type as a "valid"
// grant tag.  The IsGrantTag concept cannot reject this case
// structurally — it has no type-system handle on namespace identity.
//
// Defense in practice:
//
//   * Reviewer-visible: every cross-namespace attack must literally
//     spell `namespace crucible::fixy::grant {` outside Grant.h —
//     reviewable in any PR.
//   * CI-greppable: `scripts/check-fixy-grant-namespace-purity.sh`
//     fails the build if any file other than Grant.h reopens the
//     namespace to declare a `which_dim` specialization.
//   * Positive-attack regression: `test/safety_attack/
//     attack_fixy_grant_namespace_reopen.cpp` demonstrates the
//     residual gap exists today + documents the closure path (CI
//     grep + reviewer attention).
//
// What `IsGrantTag` DOES enforce:
//
//   * No subclass-via-extension of an already-shipped grant tag (the
//     `final` clause; FIXY-A-PLUS-1 ship + neg fixture).
//   * No "structural twin" without grant_base (cheat-probe Round 1).
//   * Missing which_dim specialization → hard error caught at
//     `engages_dim_one<D, G>` after CR-08 gating (neg fixture
//     test/fixy_neg/neg_fixy_grant_missing_which_dim.cpp).

template <typename G>
inline constexpr bool IsGrantTag_v =
       // fixy-A4-033: G must be cv-ref-free; copy-paste-from-runtime
       // bugs (decltype on a const/reference variable) reject here
       // rather than reach `which_dim_v<G>` via the resolver.
       std::is_same_v<G, std::remove_cvref_t<G>>
    && std::is_base_of_v<grant_base, G>
    && std::is_final_v<G>;

template <typename G>
concept IsGrantTag = IsGrantTag_v<G>;

// ═════════════════════════════════════════════════════════════════════
// ── fixy-M-09: structural-validation concepts for parametric tags ─
// ═════════════════════════════════════════════════════════════════════
//
// Pre-M-09 the four parametric grant tags (`protocol<Proto>`,
// `refined_with<Pred>`, `declassify<Policy>`, `from_source<Source>`)
// accepted ANY type as the parameter — `protocol<int>` /
// `refined_with<void*>` / `from_source<int>` / `declassify<int>` all
// compiled silently and produced ill-typed downstream resolutions in
// `fixy/Fn.h`'s resolver.  Each gate below names the substrate-level
// structural shape the parameter MUST satisfy, so a mistyped grant
// rejects at the fixy layer with a directed diagnostic.
//
// Discipline: each concept is the *minimum* structural bar; tighter
// gating per-axis can layer on top (e.g. fixy-H-24 already tightened
// `DeclassificationPolicy` from "any class" to "derives from
// secret_policy_base").  When a future axis adds a substrate-level
// concept matching its tag base (e.g. provenance::source_base), point
// the fixy gate at it without churning call sites.
//
// Diagnostic surface: a mistyped grant flows through the same
// `AllGrantsWellFormed` rejection path as a non-grant type (e.g.
// `int` in the pack), because the `requires`-clause failure makes
// the grant template-id ill-formed, which the `IsGrantTag` check
// catches via SFINAE.  Pairs per-gate with one fixy_neg fixture in
// `test/fixy_neg/` to witness each rejection class fires (HS14
// minimum floor) so a future loosening of any concept reddens CI.

// `IsSessionProtocol<Proto>` — gate for `protocol<Proto>`.  Accepts
// any class type: the substrate's session combinators (End / Continue
// / Send / Recv / Select / Offer / Loop / Stop_g / VendorPinned)
// AND the strict-default placeholder `safety::fn::proto::None` are
// all class types, as are user-defined state-machine tag classes
// (the doc-block at the relaxation tag explicitly admits both
// session-type AND machine state-type).  Rejects fundamental types
// (int / float / void), pointers, references, cv-qualified types,
// arrays, and function types.  Open-world by design — a tighter
// gate would require enumerating every legal substrate type and
// would block legitimate user-defined machine-state classes.
template <typename Proto>
concept IsSessionProtocol =
       std::is_same_v<Proto, std::remove_cvref_t<Proto>>
    && std::is_class_v<Proto>;

// `IsRefinementPredicate<Pred>` — gate for `refined_with<Pred>`.
// Accepts substrate-shaped predicates: empty class types like
// `safety::fn::pred::True` (exposing `check<T>(const T&)`) and
// `safety::AllOf<Preds...>` / `BoundedBelow<Min>` (exposing
// `operator()(const T&) const`).  Stricter "predicate is invocable
// on T" gating still fires at Refined<Pred, T> construction via
// `PredicateInvocableOn<Pred, T>` (per-T, can't be folded here).
//
// FIXY-FOUND-037 tightening: the gate now ALSO requires the
// substrate "empty + default-constructible" convention to defend
// against federation-cache poisoning:
//
//   `refined_with<Pred>` participates in the federation cache key
//   for every binding that engages it.  EACH UNIQUE `Pred` TYPE
//   allocates a distinct row_hash slot.  Permitting stateful
//   predicates (Pred with data members) or non-default-constructible
//   predicates would let ad-hoc per-binding closures fragment the
//   cache:
//
//     - A captures-by-reference lambda (`[&]() { ... }`) has a unique
//       closure type per declaration site AND a non-empty storage
//       footprint AND no default ctor.  Two textually-identical
//       lambdas allocate two distinct cache slots.
//     - A stateful predicate struct (`struct P { int threshold; ... };`)
//       likewise has unique type identity per declaration site.
//
//   Restricting the gate to empty + default-constructible types
//   excludes the closure / stateful-struct cases at compile time —
//   bindings must use NAMED substrate predicates (which share
//   types across call sites and thus cache slots).  Capture-less
//   lambdas ARE empty + default-constructible in C++20+, so they
//   still pass; the gate only rejects the genuinely-problematic
//   stateful shapes.
template <typename Pred>
concept IsRefinementPredicate =
       std::is_same_v<Pred, std::remove_cvref_t<Pred>>
    && std::is_class_v<Pred>
    && std::is_empty_v<Pred>                   // FIXY-FOUND-037: substrate convention
    && std::is_default_constructible_v<Pred>;  // FIXY-FOUND-037: required for substrate use

// `IsProvenanceSource<Source>` — gate for `from_source<Source>`.
// Substrate convention: every `safety::source::*` tag is an empty
// marker class (`struct FromUser {};`, `struct External {};`,
// `struct JsonRegistry {};`, etc. — see Tagged.h `namespace source`).
// The gate enforces the empty-marker shape at the fixy layer so a
// future source tag that violates the convention (carries state,
// inherits from a heavyweight base) surfaces here rather than
// silently breaking downstream resolution.  Stricter than the other
// two concepts because the substrate's convention IS stricter.
//
// FIXY-FOUND-044 audit conclusion (2026-05-25): the ticket's premise
// "misses source::ForgePhase" is incorrect.  `ForgePhase<Phase>` is
// an NTTP-parameterized class template (`template <char Phase> struct
// ForgePhase { static_assert(...) }`) with NO data members — the
// `Phase` parameter is a non-type template parameter, NOT a stored
// field.  `std::is_empty_v<ForgePhase<'F'>>` is true; Tagged.h:327
// explicitly documents "sizeof(ForgePhase<P>) is 1 but EBO-collapses
// in Tagged<T, ForgePhase<P>> per the standard Graded regime-1
// storage rule."  IsProvenanceSource ACCEPTS every legal ForgePhase
// instantiation today.  The sentinel block below pins this for all
// 12 forge phases (A=INGEST..L=VALIDATE) so a future refactor that
// silently broke the empty-marker shape of ForgePhase would red
// this site.
template <typename Source>
concept IsProvenanceSource =
       std::is_same_v<Source, std::remove_cvref_t<Source>>
    && std::is_class_v<Source>
    && std::is_empty_v<Source>;

// ═════════════════════════════════════════════════════════════════════
// ── which_dim_v<G> — primary template + per-tag specialization ─────
// ═════════════════════════════════════════════════════════════════════
//
// Tells Reject.h which dimension a given grant tag engages with.  An
// unrecognized tag (no specialization) is a hard error caught at the
// `Engaged<F, Dim>` check site.

template <typename G>
struct which_dim;  // primary — left undefined; specialize per tag

template <typename G>
inline constexpr dim::DimensionAxis which_dim_v = which_dim<G>::value;

// ═════════════════════════════════════════════════════════════════════
// ── Acceptance markers — one per DimensionAxis ─────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// `accept_default_strict_for<D>` says: "I have read the discipline
// for axis D and choose the strict default."  These tags are pure
// engagement markers; resolution to `Fn<>`'s default is automatic
// (`fixy/Fn.h`'s `detail::resolve` namespace reads
// `strict_default_for<D>::type` or `::value`).
//
// The Type axis exposes a marker too even though it has no strict
// default — the marker means "I am binding this function to its own
// declared type" and is implied automatically by `fixy::fn<Type, ...>`
// via the implicit Type engagement injected at the resolver site.

template <dim::DimensionAxis D>
struct accept_default_strict_for final : grant_base {};

template <dim::DimensionAxis D>
struct which_dim<accept_default_strict_for<D>>
    : std::integral_constant<dim::DimensionAxis, D> {};

// ═════════════════════════════════════════════════════════════════════
// ── Relaxation tags ─────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Per-axis relaxations.  Each tag:
//   1. inherits grant_base + final
//   2. specializes which_dim with its DimensionAxis
//   3. is empty (parameters live in the template, not as fields)
//
// The actual mapping of "what does this tag mean inside Fn<>?" is the
// job of `fixy/Fn.h`'s `detail::resolve` namespace.  IsAccepted only
// needs the dim-mapping for the engagement check.

// ── DimensionAxis::Usage = 2 relaxations ───────────────────────────
struct affine          final : grant_base {};  // Usage = Affine
struct copy            final : grant_base {};  // Usage = Copy
struct ghost           final : grant_base {};  // Usage = Ghost
struct borrow          final : grant_base {};  // Usage = Borrow
struct capability_usage final : grant_base {};  // Usage = Capability
//   (`capability_usage` not `capability` — avoid clash with the
//    Effect-axis capability concept.  fixy.md §24.2 precedent.)

template <> struct which_dim<affine>           : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Usage> {};
template <> struct which_dim<copy>             : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Usage> {};
template <> struct which_dim<ghost>            : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Usage> {};
template <> struct which_dim<borrow>           : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Usage> {};
template <> struct which_dim<capability_usage> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Usage> {};

// ── DimensionAxis::Effect = 3 relaxations ──────────────────────────
//
// `with<Es...>` engages the Effect axis with an explicit effects::Row
// of the supplied effects.  The Es pack is `effects::Effect` enum
// values; `fixy/Fn.h`'s `detail::resolve` namespace projects them
// into `effects::Row<Es...>`.
//
// FIXY-FOUND-040 — pure-effect audit equivalence:
//   `with<>` (EMPTY pack) is SEMANTICALLY EQUIVALENT to
//   `accept_default_strict_for<DimensionAxis::Effect>` because
//   `strict_default_for<Effect>::type == effects::Row<>` (Default.h
//   §157).  An auditor searching for "every pure-effect binding"
//   MUST recognise both forms — use the
//   `Theory.h::is_pure_effect_grant_v<G>` predicate, which
//   specializes on BOTH the positive `with<>` form and the deferred
//   strict-default form (mirroring the is_secret_grant design
//   established by fixy-CR-01).  Direct grep for `with<>` alone
//   misses the deferred-form sites; the predicate is the canonical
//   single-grep audit anchor.

template <effects::Effect... Es>
struct with final : grant_base {};

template <effects::Effect... Es>
struct which_dim<with<Es...>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Effect> {};

// Convenience subtagged aliases — `with_alloc`, `with_io`, `with_bg`
// engage the Effect axis with single-effect rows (the most common
// production case).  Each is final + grant_base via the primary
// `with<>` specialization.

using with_alloc = with<effects::Effect::Alloc>;
using with_io    = with<effects::Effect::IO>;
using with_block = with<effects::Effect::Block>;
using with_bg    = with<effects::Effect::Bg>;
using with_init  = with<effects::Effect::Init>;
using with_test  = with<effects::Effect::Test>;

// ── DimensionAxis::Security = 4 relaxations ────────────────────────
//
// `declassify<Policy>` engages Security with a named policy tag,
// projecting to `SecLevel::Public`.  The Policy parameter is
// captured for audit-trail purposes by IsAccepted; the policy catalog
// ships under safety::secret_policy::* and is consumed by
// `fixy/Fn.h`'s `detail::resolve` namespace + the declassification
// call sites.
//
// Five more relaxation tags expose the remaining SecLevel lattice
// points so callers can reach every value of the security lattice
// (Unclassified < Public < Internal < Classified < Secret) through
// a single explicit grant rather than relying on the strict default
// alone.  Conventions:
//
//   - `as_unclassified`  — pin Security to SecLevel::Unclassified.
//                          The lattice bottom; suitable for fully
//                          public emission paths whose output carries
//                          NO confidentiality obligation.
//   - `as_public`        — pin Security to SecLevel::Public.  Distinct
//                          from `declassify<P>` because this form does
//                          not carry a Policy tag: the binding asserts
//                          the data was never classified to begin with.
//   - `as_internal`      — pin Security to SecLevel::Internal.  The
//                          "org-internal" tier — observable inside the
//                          organization, not for external emission.
//   - `as_classified`    — pin Security to SecLevel::Classified (the
//                          strict default).  Explicit form for
//                          self-documenting bindings that want a named
//                          relaxation rather than the implicit
//                          accept-default marker.
//   - `as_secret`        — pin Security to SecLevel::Secret.  Top of
//                          the lattice; the value MUST NOT be
//                          declassified.  Required for crypto keys,
//                          Cipher encryption keys, private weights.

template <typename Policy>
    requires ::crucible::safety::DeclassificationPolicy<Policy>
struct declassify final : grant_base {};

template <typename Policy>
    requires ::crucible::safety::DeclassificationPolicy<Policy>
struct which_dim<declassify<Policy>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Security> {};

struct as_unclassified final : grant_base {};
struct as_public       final : grant_base {};
struct as_internal     final : grant_base {};
struct as_classified   final : grant_base {};
struct as_secret       final : grant_base {};

template <> struct which_dim<as_unclassified>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Security> {};
template <> struct which_dim<as_public>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Security> {};
template <> struct which_dim<as_internal>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Security> {};
template <> struct which_dim<as_classified>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Security> {};
template <> struct which_dim<as_secret>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Security> {};

// ── DimensionAxis::Protocol = 5 relaxations ────────────────────────
//
// `protocol<Proto>` engages Protocol with a session-type or machine
// state-type.  `fixy/Fn.h`'s `detail::resolve` namespace projects
// to the `safety::fn::Protocol` slot.

template <typename Proto>
    requires IsSessionProtocol<Proto>
struct protocol final : grant_base {};

template <typename Proto>
    requires IsSessionProtocol<Proto>
struct which_dim<protocol<Proto>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Protocol> {};

// ── DimensionAxis::Lifetime = 6 relaxations ────────────────────────
//
// `in_region<RegionTag>` engages Lifetime with a named region tag,
// projecting to `safety::fn::lifetime::In<RegionTag>`.

template <auto RegionTag>
struct in_region final : grant_base {};

template <auto RegionTag>
struct which_dim<in_region<RegionTag>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Lifetime> {};

// ── DimensionAxis::Provenance = 7 relaxations ──────────────────────
//
// `from_source<Source>` engages Provenance with a substrate source
// tag (source::FromUser / source::FromDb / source::FromNetwork /
// etc.).  `fixy/Fn.h`'s `detail::resolve` namespace projects to
// the `safety::fn::source_t` slot.

template <typename Source>
    requires IsProvenanceSource<Source>
struct from_source final : grant_base {};

template <typename Source>
    requires IsProvenanceSource<Source>
struct which_dim<from_source<Source>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Provenance> {};

// ── DimensionAxis::Trust = 8 relaxations ───────────────────────────
//
// `trust_assumed<auto Rationale>` engages Trust with a documented
// rationale string (the rationale is a non-type template parameter
// — typically a `std::array<char, N>` literal — captured for audit
// trails but treated opaquely by IsAccepted).  `fixy/Fn.h`'s
// `detail::resolve` namespace projects to `safety::trust::Assumed`.
//
// Four additional relaxation tags expose the remaining lattice points
// of safety::trust::* so every named trust level is reachable through
// fixy::: Verified (strict default), Tested, Unverified, Assumed
// (already covered by trust_assumed), External.  Conventions:
//
//   - `trust_verified`   — pin Trust to safety::trust::Verified.
//                          Explicit form of the strict default; useful
//                          for self-documenting bindings.
//   - `trust_tested`     — pin Trust to safety::trust::Tested.  The
//                          value is covered by tests but lacks formal
//                          coverage — typical for code under active
//                          development.
//   - `trust_unverified` — pin Trust to safety::trust::Unverified.  No
//                          formal coverage at all; requires reviewer
//                          attention before reaching production.
//   - `trust_external`   — pin Trust to safety::trust::External.  Trust
//                          delegated to a foreign source (vendor library,
//                          firmware, BMC).  Resolution time MUST verify
//                          the upstream's claim through a separate
//                          channel.

// FIXY-FOUND-038 closure: the auto NTTP carries the audit rationale.
// Pre-FOUND-038, `Rationale = 0` was the default — `trust_assumed<>` at
// a production call site silently produced `Rationale == 0` indistinguish-
// able from an explicit-zero choice, defeating audit grep (`grep
// trust_assumed<` returns hits, but the auditor cannot tell "I provided
// no rationale" from "I deliberately chose 0").
//
// The default is removed: every production use MUST provide an explicit
// rationale value (typically a `std::array<char, N>` literal capturing
// the human-readable justification, e.g.
//   `trust_assumed<std::array{"vendor-firmware-signed-by-NVIDIA"}>`).
// Type-trait queries that don't care about the rationale value (e.g.
// `which_dim_v<trust_assumed<axis_query_tag>>`) provide the literal
// sentinel value `axis_query_tag` (defined immediately below).
//
// The audit-grep discipline: every `trust_assumed<` site must surface
// a meaningful rationale on review; sites bearing `axis_query_tag`
// are by construction type-trait queries and skipped by the auditor.
inline constexpr int axis_query_tag = 0;

template <auto Rationale>
struct trust_assumed final : grant_base {};

template <auto Rationale>
struct which_dim<trust_assumed<Rationale>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Trust> {};

struct trust_verified   final : grant_base {};
struct trust_tested     final : grant_base {};
struct trust_unverified final : grant_base {};
struct trust_external   final : grant_base {};

template <> struct which_dim<trust_verified>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Trust> {};
template <> struct which_dim<trust_tested>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Trust> {};
template <> struct which_dim<trust_unverified>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Trust> {};
template <> struct which_dim<trust_external>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Trust> {};

// ── DimensionAxis::Representation = 9 relaxations ──────────────────
//
// `repr<Kind>` engages Representation with a ReprKind enum value.

template <safety::fn::ReprKind Kind>
struct repr final : grant_base {};

template <safety::fn::ReprKind Kind>
struct which_dim<repr<Kind>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Representation> {};

// ── DimensionAxis::Observability = 10 — derived from Effect ────────
//
// Observability is derived from EffectRow per fixy.md §24.1.  The
// only valid grant on this axis is the acceptance marker; explicit
// relaxation would be inconsistent with the derivation rule.  We
// ship NO relaxation tag for Observability — only
// `accept_default_strict_for<Observability>` is legal.

// ── DimensionAxis::Complexity = 11 relaxations ─────────────────────
//
// `cost_constant`, `cost_linear<N>`, `cost_quadratic<N>` engage
// Complexity with the corresponding `safety::fn::cost::*` tag.

struct cost_constant   final : grant_base {};
template <auto N> struct cost_linear     final : grant_base {};
template <auto N> struct cost_quadratic  final : grant_base {};
struct cost_unbounded  final : grant_base {};

template <>            struct which_dim<cost_constant>      : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Complexity> {};
template <auto N>      struct which_dim<cost_linear<N>>     : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Complexity> {};
template <auto N>      struct which_dim<cost_quadratic<N>>  : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Complexity> {};
template <>            struct which_dim<cost_unbounded>     : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Complexity> {};

// ── DimensionAxis::Precision = 12 relaxations ──────────────────────
//
// `precision_f32`, `precision_f64`, `precision_higham<Bound>` —
// `fixy/Fn.h`'s `detail::resolve` namespace projects to
// `safety::fn::precision::*`.

struct precision_f32   final : grant_base {};
struct precision_f64   final : grant_base {};
template <auto Bound> struct precision_higham final : grant_base {};

template <>           struct which_dim<precision_f32>          : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Precision> {};
template <>           struct which_dim<precision_f64>          : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Precision> {};
template <auto Bound> struct which_dim<precision_higham<Bound>>: std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Precision> {};

// ── DimensionAxis::Space = 13 relaxations ──────────────────────────
template <auto N> struct space_bounded final : grant_base {};
struct space_unbounded final : grant_base {};

template <auto N> struct which_dim<space_bounded<N>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Space> {};
template <>       struct which_dim<space_unbounded>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Space> {};

// ── DimensionAxis::Overflow = 14 relaxations ───────────────────────
struct overflow_wrap     final : grant_base {};
struct overflow_saturate final : grant_base {};
struct overflow_widen    final : grant_base {};

template <> struct which_dim<overflow_wrap>     : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Overflow> {};
template <> struct which_dim<overflow_saturate> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Overflow> {};
template <> struct which_dim<overflow_widen>    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Overflow> {};

// ── DimensionAxis::Mutation = 15 relaxations ───────────────────────
struct mut_mutable    final : grant_base {};
struct mut_append     final : grant_base {};
struct mut_monotonic  final : grant_base {};

template <> struct which_dim<mut_mutable>   : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Mutation> {};
template <> struct which_dim<mut_append>    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Mutation> {};
template <> struct which_dim<mut_monotonic> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Mutation> {};

// ── DimensionAxis::Reentrancy = 16 relaxations ─────────────────────
//
// FIXY-FOUND-036: naming convention.  ControlFlow grants live under
// `grant::ctrl::*` (FIXY-V-244) — e.g. `grant::ctrl::throws`,
// `grant::ctrl::rationale`.  Reentrancy grants live at top-level
// `grant::*` for historical reasons (`grant::reentrant`,
// `grant::coroutine`).  This asymmetry was an audit-grep hazard:
// `grant::coroutine` resolves to the REENTRANCY axis, NOT a
// ControlFlow "may use C++ coroutines" grant — but a reader seeing
// only the unqualified spelling cannot tell which axis it engages.
//
// FOUND-036 closure provides a `grant::reentrancy::*` sub-namespace
// alias matching the ctrl:: pattern.  Production sites SHOULD prefer
// the qualified spelling (`grant::reentrancy::coroutine`,
// `grant::reentrancy::reentrant`); the top-level forms remain valid
// for backwards compatibility.  A future `grant::ctrl::coroutine_*`
// grant (when ControlFlow needs to express "callable suspends via
// co_yield/co_await") would land WITHOUT colliding because the
// namespace prefix disambiguates.
struct reentrant final : grant_base {};
struct coroutine final : grant_base {};

template <> struct which_dim<reentrant> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Reentrancy> {};
template <> struct which_dim<coroutine> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Reentrancy> {};

// FIXY-FOUND-036: canonical sub-namespace, mirroring grant::ctrl::*
// for ControlFlow.  New call sites should prefer this qualified
// spelling; top-level `grant::coroutine` / `grant::reentrant` remain
// valid aliases for backwards-compat with FIXY-U-* production sites.
namespace reentrancy {
    using reentrant = ::crucible::fixy::grant::reentrant;
    using coroutine = ::crucible::fixy::grant::coroutine;
}  // namespace reentrancy

// Cross-axis non-collision sentinel: both spellings (top-level and
// `reentrancy::`) MUST project to the same Reentrancy axis.  If a
// future refactor accidentally re-homes one to a different axis,
// or — worse — adds a `grant::ctrl::coroutine` that ALSO claims
// Reentrancy, the dispatch ambiguity would fire here at compile
// time before the regression can ship.
static_assert(std::is_same_v<reentrancy::coroutine, coroutine>,
    "FIXY-FOUND-036: grant::reentrancy::coroutine must alias the "
    "top-level grant::coroutine — sub-namespace is a re-export, "
    "not a separate type.");
static_assert(std::is_same_v<reentrancy::reentrant, reentrant>,
    "FIXY-FOUND-036: grant::reentrancy::reentrant must alias the "
    "top-level grant::reentrant — sub-namespace is a re-export.");
static_assert(which_dim<reentrancy::coroutine>::value
              == dim::DimensionAxis::Reentrancy,
    "FIXY-FOUND-036: grant::reentrancy::coroutine must resolve to "
    "DimensionAxis::Reentrancy (forward-looking guard against a "
    "future grant::ctrl::coroutine that might silently claim the "
    "same name on a different axis).");
static_assert(which_dim<reentrancy::reentrant>::value
              == dim::DimensionAxis::Reentrancy,
    "FIXY-FOUND-036: grant::reentrancy::reentrant must resolve to "
    "DimensionAxis::Reentrancy.");

// ── DimensionAxis::Size = 17 relaxations ───────────────────────────
template <auto Depth> struct sized_at final : grant_base {};
struct productive final : grant_base {};

template <auto Depth> struct which_dim<sized_at<Depth>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Size> {};
template <>           struct which_dim<productive>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Size> {};

// ── DimensionAxis::Version = 18 relaxations ────────────────────────
template <std::uint32_t V> struct version final : grant_base {};

template <std::uint32_t V> struct which_dim<version<V>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Version> {};

// ── DimensionAxis::Staleness = 19 relaxations ──────────────────────
template <auto TauMax> struct stale_to final : grant_base {};

template <auto TauMax> struct which_dim<stale_to<TauMax>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Staleness> {};

// ── DimensionAxis::Refinement = 1 relaxations ──────────────────────
//
// `refined_with<Pred>` engages Refinement with a predicate type that
// satisfies the `pred::check` interface.  `fixy/Fn.h`'s
// `detail::resolve` namespace projects to `Fn<Type, Pred, ...>`.

template <typename Pred>
    requires IsRefinementPredicate<Pred>
struct refined_with final : grant_base {};

template <typename Pred>
    requires IsRefinementPredicate<Pred>
struct which_dim<refined_with<Pred>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Refinement> {};

// ── DimensionAxis::Type = 0 relaxations ────────────────────────────
//
// The Type axis is caller-supplied via `fixy::fn<Type, Grants...>`'s
// first template parameter.  `fixy/Fn.h`'s `detail::resolve`
// namespace synthesizes an implicit
// `accept_default_strict_for<Type>` engagement marker at
// `fixy::fn<>` construction time, so callers do NOT need to write
// the marker.  There is no Type-axis relaxation tag — the Type IS
// the parameter.

// ═════════════════════════════════════════════════════════════════════
// ── Self-test (compile-time) ───────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail::grant_self_test {

// Every shipped grant tag must inherit grant_base + be final.
// IsGrantTag checks both; sampling representative tags here, the
// cheat-probe round catches subclass attempts and missing-final
// attempts.

static_assert(IsGrantTag<accept_default_strict_for<dim::DimensionAxis::Usage>>);
static_assert(IsGrantTag<affine>);
static_assert(IsGrantTag<copy>);
static_assert(IsGrantTag<ghost>);
static_assert(IsGrantTag<borrow>);
static_assert(IsGrantTag<capability_usage>);
static_assert(IsGrantTag<with<effects::Effect::IO>>);
static_assert(IsGrantTag<with<>>);
// fixy-M-09: declassify<P> now requires DeclassificationPolicy<P>
// (P derives from secret_policy::secret_policy_base).  Sample with a
// real policy tag from safety/Secret.h's catalog; `declassify<int>`
// is rejected at template instantiation (witnessed by the fixy_neg
// fixture neg_fixy_grant_declassify_non_policy.cpp).
static_assert(IsGrantTag<declassify<
    ::crucible::safety::secret_policy::AuditedLogging>>);
static_assert(IsGrantTag<as_unclassified>);
static_assert(IsGrantTag<as_public>);
static_assert(IsGrantTag<as_internal>);
static_assert(IsGrantTag<as_classified>);
static_assert(IsGrantTag<as_secret>);
static_assert(IsGrantTag<trust_verified>);
static_assert(IsGrantTag<trust_tested>);
static_assert(IsGrantTag<trust_unverified>);
static_assert(IsGrantTag<trust_external>);
static_assert(IsGrantTag<refined_with<safety::fn::pred::True>>);

// ── FIXY-FOUND-037 sentinels: IsRefinementPredicate gate tightening ──
//
// Positive: substrate predicates (empty + default-constructible)
// continue to pass.  pred::True is the canonical example.
static_assert(IsRefinementPredicate<safety::fn::pred::True>,
    "FIXY-FOUND-037: safety::fn::pred::True must satisfy the "
    "tightened IsRefinementPredicate gate (empty + default-ctorable).");

// Negative: a stateful predicate-shaped struct is REJECTED by the
// tightened gate.  Demonstrates that the federation-cache-poisoning
// surface (per-binding stateful predicates fragmenting cache slots)
// is closed at compile time, not deferred to Refined<Pred, T>
// construction.  Reviewers extending the catalog with a new predicate
// must either make it empty + default-ctorable (substrate convention)
// or document a deliberate opt-out.
namespace detail::found_037_witness {
    struct StatefulPredicate {                   // NOT empty — has data
        int threshold = 0;
        [[nodiscard]] constexpr bool operator()(int v) const noexcept
            { return v > threshold; }
    };
    static_assert(!IsRefinementPredicate<StatefulPredicate>,
        "FIXY-FOUND-037: stateful predicates must be rejected by the "
        "tightened IsRefinementPredicate gate — each unique stateful "
        "Pred type would fragment the federation cache.");

    struct NonDefaultConstructiblePredicate {    // empty but not default-ctorable
        constexpr NonDefaultConstructiblePredicate(int) noexcept {}
        [[nodiscard]] constexpr bool operator()(int v) const noexcept
            { return v > 0; }
    };
    static_assert(!IsRefinementPredicate<NonDefaultConstructiblePredicate>,
        "FIXY-FOUND-037: non-default-constructible predicates must be "
        "rejected — the substrate Refined<Pred, T> machinery assumes "
        "Pred is freely instantiable.");

    // Conversely: a capture-less lambda IS empty + default-constructible
    // in C++20+, so the tightening does NOT reject this common shape
    // (the gate only rejects genuinely-problematic stateful shapes).
    using CaptureLessLambdaType = decltype([](int v) noexcept { return v > 0; });
    static_assert(std::is_empty_v<CaptureLessLambdaType>);
    static_assert(std::is_default_constructible_v<CaptureLessLambdaType>);
    static_assert(IsRefinementPredicate<CaptureLessLambdaType>,
        "FIXY-FOUND-037: capture-less lambdas in C++20+ are empty + "
        "default-constructible, so the tightening admits them.  This "
        "is by design — the lambda's UNIQUE TYPE per declaration site "
        "is the audit-discoverable cache-slot identity.");
}  // namespace detail::found_037_witness
static_assert(IsGrantTag<version<3>>);
static_assert(IsGrantTag<stale_to<5>>);

// EBO-collapse witnesses for the new Security/Trust lattice tags —
// each empty + final + grant_base → 1-byte tag.
static_assert(sizeof(as_unclassified)   == 1);
static_assert(sizeof(as_public)         == 1);
static_assert(sizeof(as_internal)       == 1);
static_assert(sizeof(as_classified)     == 1);
static_assert(sizeof(as_secret)         == 1);
static_assert(sizeof(trust_verified)    == 1);
static_assert(sizeof(trust_tested)      == 1);
static_assert(sizeof(trust_unverified)  == 1);
static_assert(sizeof(trust_external)    == 1);

// which_dim_v round-trip for the new Security lattice tags.
static_assert(which_dim_v<as_unclassified>   == dim::DimensionAxis::Security);
static_assert(which_dim_v<as_public>         == dim::DimensionAxis::Security);
static_assert(which_dim_v<as_internal>       == dim::DimensionAxis::Security);
static_assert(which_dim_v<as_classified>     == dim::DimensionAxis::Security);
static_assert(which_dim_v<as_secret>         == dim::DimensionAxis::Security);

// which_dim_v round-trip for the new Trust lattice tags.
static_assert(which_dim_v<trust_verified>    == dim::DimensionAxis::Trust);
static_assert(which_dim_v<trust_tested>      == dim::DimensionAxis::Trust);
static_assert(which_dim_v<trust_unverified>  == dim::DimensionAxis::Trust);
static_assert(which_dim_v<trust_external>    == dim::DimensionAxis::Trust);

// EBO-collapse witness — empty + final + grant_base = 1-byte tag.
static_assert(sizeof(affine)                 == 1);
static_assert(sizeof(copy)                   == 1);
static_assert(sizeof(ghost)                  == 1);
static_assert(sizeof(with<>)                 == 1);
static_assert(sizeof(with<effects::Effect::Bg, effects::Effect::IO>) == 1);
static_assert(sizeof(accept_default_strict_for<dim::DimensionAxis::Trust>) == 1);

// which_dim_v round-trip — every dim's tags route to that dim.
static_assert(which_dim_v<affine>                                            == dim::DimensionAxis::Usage);
static_assert(which_dim_v<copy>                                              == dim::DimensionAxis::Usage);
static_assert(which_dim_v<with<effects::Effect::IO>>                         == dim::DimensionAxis::Effect);
static_assert(which_dim_v<declassify<
    ::crucible::safety::secret_policy::AuditedLogging>>                      == dim::DimensionAxis::Security);
static_assert(which_dim_v<refined_with<safety::fn::pred::True>>              == dim::DimensionAxis::Refinement);
static_assert(which_dim_v<accept_default_strict_for<dim::DimensionAxis::Trust>> == dim::DimensionAxis::Trust);
static_assert(which_dim_v<version<3>>                                        == dim::DimensionAxis::Version);
static_assert(which_dim_v<stale_to<5>>                                       == dim::DimensionAxis::Staleness);
static_assert(which_dim_v<repr<safety::fn::ReprKind::C>>                     == dim::DimensionAxis::Representation);
static_assert(which_dim_v<overflow_wrap>                                     == dim::DimensionAxis::Overflow);
static_assert(which_dim_v<mut_mutable>                                       == dim::DimensionAxis::Mutation);
static_assert(which_dim_v<reentrant>                                         == dim::DimensionAxis::Reentrancy);
static_assert(which_dim_v<cost_constant>                                     == dim::DimensionAxis::Complexity);
static_assert(which_dim_v<precision_f64>                                     == dim::DimensionAxis::Precision);
static_assert(which_dim_v<space_unbounded>                                   == dim::DimensionAxis::Space);
static_assert(which_dim_v<productive>                                        == dim::DimensionAxis::Size);
static_assert(which_dim_v<in_region<0>>                                      == dim::DimensionAxis::Lifetime);
static_assert(which_dim_v<from_source<safety::source::FromUser>>             == dim::DimensionAxis::Provenance);
// FIXY-FOUND-044: pin IsProvenanceSource ACCEPTS every legal
// ForgePhase<P> for P in A..L (Forge 12-phase pipeline).  The audit
// ticket framed this as "misses source::ForgePhase" but ForgePhase
// is empty-by-construction (NTTP `char Phase`, no data members) per
// Tagged.h:327; the concept ACCEPTS all 12 instantiations.  A future
// refactor that broke the empty-marker shape of ForgePhase would
// red this site directly, surfacing the regression at the concept
// boundary rather than downstream at `from_source<>` use sites.
static_assert(IsProvenanceSource<safety::source::ForgePhase<'A'>>);  // INGEST
static_assert(IsProvenanceSource<safety::source::ForgePhase<'B'>>);  // ANALYZE
static_assert(IsProvenanceSource<safety::source::ForgePhase<'C'>>);  // REWRITE
static_assert(IsProvenanceSource<safety::source::ForgePhase<'D'>>);  // FUSE
static_assert(IsProvenanceSource<safety::source::ForgePhase<'E'>>);  // LOWER_TO_KERNELS
static_assert(IsProvenanceSource<safety::source::ForgePhase<'F'>>);  // TILE
static_assert(IsProvenanceSource<safety::source::ForgePhase<'G'>>);  // MEMPLAN
static_assert(IsProvenanceSource<safety::source::ForgePhase<'H'>>);  // COMPILE
static_assert(IsProvenanceSource<safety::source::ForgePhase<'I'>>);  // SCHEDULE
static_assert(IsProvenanceSource<safety::source::ForgePhase<'J'>>);  // EMIT
static_assert(IsProvenanceSource<safety::source::ForgePhase<'K'>>);  // DISTRIBUTE
static_assert(IsProvenanceSource<safety::source::ForgePhase<'L'>>);  // VALIDATE
static_assert(which_dim_v<from_source<safety::source::ForgePhase<'F'>>>      == dim::DimensionAxis::Provenance);
static_assert(which_dim_v<trust_assumed<axis_query_tag>>                     == dim::DimensionAxis::Trust);
static_assert(which_dim_v<protocol<safety::fn::proto::None>>                 == dim::DimensionAxis::Protocol);

// ── fixy-A4-033: cv-ref rejection witnesses ────────────────────────
//
// Three classes of copy-paste-from-runtime mistake must be rejected:
//   * const-qualified grant (`const auto g = affine{}; decltype(g)`)
//   * volatile-qualified grant (rare; uniform discipline)
//   * reference-qualified grant (return value from `g_ref()`)
//
// The pre-A4-033 form (which stripped cv-ref before checking) made
// every line below ACCEPT — silently coercing the bug to its bare
// form.  The tightened form REJECTS all six, forcing the caller to
// spell the grant tag's bare type.

// Positive control — bare grant tags accept.
static_assert(IsGrantTag_v<affine>);
static_assert(IsGrantTag_v<copy>);
static_assert(IsGrantTag_v<as_public>);

// cv-qualified — REJECT.
static_assert(!IsGrantTag_v<const affine>);
static_assert(!IsGrantTag_v<volatile affine>);
static_assert(!IsGrantTag_v<const volatile affine>);

// Reference-qualified — REJECT.
static_assert(!IsGrantTag_v<affine&>);
static_assert(!IsGrantTag_v<const affine&>);
static_assert(!IsGrantTag_v<affine&&>);

}  // namespace detail::grant_self_test

}  // namespace crucible::fixy::grant
