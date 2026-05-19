#pragma once

// ── crucible::fixy::diag — per-tag insight specializations ─────────
//
// fixy-H-14: the 20 `FixyNotEngaged_<Axis>` tags + the 4 §30.14
// theory-corpus entries each ship an `insight_provider` specialization
// here so the deep-diagnostic builder emits structured Why / Symptom /
// Correct-example / Violating-example sections when the gate fires.
// Without these specializations the corresponding insight_provider
// primary template (in safety/diag/Insights.h) returns empty defaults
// and `has_insights_v<Tag>` is false — the deep builder collapses to
// the bare 8-line format, and the load-bearing prose pointing the
// reader at the right grant tag / declassification policy is lost.
//
// ── Substrate consumed ─────────────────────────────────────────────
//
//   safety/diag/Insights.h::CRUCIBLE_DEFINE_INSIGHTS_QV — the macro
//                                                         (QV variant
//                                                         locks the
//                                                         30/20/10/10
//                                                         minimum-
//                                                         length bar).
//   fixy/Reject.h::FixyNotEngaged_*  — the 20 axis-engagement tags.
//   fixy/Theory.h::corpus::*         — the 4 §30.14 unsoundness tags.
//
// ── Authoring discipline ───────────────────────────────────────────
//
// Per safety/diag/Insights.h §"Authoring discipline":
//   * why_this_matters — architectural rationale + paper / docs cite
//   * symptom_pattern  — typical bad shape at call site
//   * correct_example  — one line of compliant C++
//   * violating_example — one line of anti-pattern
//
// All four prose fields meet the QV minimums (30 / 20 / 10 / 10
// chars) so `has_substantive_insights_v<Tag>` is true for every
// specialized tag.  Sentinel TU `test/test_fixy_insights.cpp`
// enforces this at build time.
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe — all fields are constexpr string_view of constexpr
//              literal data; never uninit-read.
//   TypeSafe — specializations are tag-keyed; no narrowing.
//   NullSafe — string_view fields point into program rodata; never
//              null after value-init.
//   MemSafe  — no allocation; constexpr-only entity.
//   DetSafe  — same Tag → same insight; deterministic across builds.
//
// ── Runtime cost ───────────────────────────────────────────────────
//
// Zero.  These are constexpr specializations consulted only at
// compile time (or at hand-written diagnostic-format call sites).

#include <crucible/fixy/Reject.h>     // FixyNotEngaged_*  (and pulls Theory.h)
#include <crucible/fixy/Theory.h>     // corpus::* entries
#include <crucible/safety/diag/Insights.h>

// ═══════════════════════════════════════════════════════════════════
// ── 20 FixyNotEngaged_<Axis> specializations ──────────────────────
// ═══════════════════════════════════════════════════════════════════

CRUCIBLE_DEFINE_INSIGHTS_QV(
    ::crucible::fixy::diag::FixyNotEngaged_Type,
    ::crucible::safety::diag::Severity::Error,
    "The Type axis names the function/callable's principal type and is the "
    "anchor for §6.8 collision rules in safety/CollisionCatalog.h.  "
    "fixy::fn<T, ...> implicitly mints the Type marker (FIXY-AUDIT-A7); "
    "this diagnostic fires when IsAccepted is invoked directly without it.",
    "User wrote IsAccepted<Grants...> at namespace scope with no Type marker "
    "passed (also no fixy::fn<> wrapper to inject one).",
    "fixy::fn<MyCallable, grant::with<>, ...>(MyCallable{});",
    "static_assert(IsAccepted<Grants...>);  // no Type → engagement-incomplete");

CRUCIBLE_DEFINE_INSIGHTS_QV(
    ::crucible::fixy::diag::FixyNotEngaged_Refinement,
    ::crucible::safety::diag::Severity::Error,
    "Refinement engages value-level predicates (Refined<P, T> family).  "
    "Strict default is the identity predicate is_no_op_pred; explicit "
    "grant::refined_with<P> tightens.  Missing engagement means the gate "
    "can't tell whether the author intended no refinement or forgot.",
    "Grants pack omits both grant::accept_default_strict_for<Refinement> "
    "and grant::refined_with<P>.",
    "grant::accept_default_strict_for<dim::DimensionAxis::Refinement>",
    "fixy::fn<T, /* no Refinement grant */, ...>");

CRUCIBLE_DEFINE_INSIGHTS_QV(
    ::crucible::fixy::diag::FixyNotEngaged_Usage,
    ::crucible::safety::diag::Severity::Error,
    "Usage encodes ownership discipline (Linear / Affine / Copy / Ghost).  "
    "CSL frame-rule soundness depends on a declared mode; the strict "
    "default Linear consumes by move, Copy/Affine relax.  Without "
    "engagement, §6.8 L004/L002/L003 collision rules can't gate.",
    "Grants pack omits grant::as_linear / as_affine / as_copy / ghost "
    "AND omits accept_default_strict_for<Usage>.",
    "grant::as_linear  // or as_copy / as_affine / ghost",
    "fixy::fn<T, /* no Usage grant */, ...>");

CRUCIBLE_DEFINE_INSIGHTS_QV(
    ::crucible::fixy::diag::FixyNotEngaged_Effect,
    ::crucible::safety::diag::Severity::Error,
    "Effect declares the Met(X) row of OS-effects (Alloc/IO/Block/Bg/Init/"
    "Test).  Strict default is empty Row<>.  Missing engagement leaves "
    "Subrow admission gates unable to verify ctx-fit at call sites; "
    "production deps assume the row is declared somewhere.",
    "Grants pack omits grant::with<...> AND omits "
    "accept_default_strict_for<Effect>.",
    "grant::with<effects::Effect::IO>",
    "fixy::fn<T, /* no Effect grant */, ...>");

CRUCIBLE_DEFINE_INSIGHTS_QV(
    ::crucible::fixy::diag::FixyNotEngaged_Security,
    ::crucible::safety::diag::Severity::Error,
    "Security engages information-flow level (Public/Classified/Secret).  "
    "Strict default is Classified per fixy-CR-01 + 16_05_2026_fixy.md §3 — "
    "this is load-bearing for §30.14 theory-corpus IFC detection.  Without "
    "engagement, classified flow into IO/Bg slips past Theory.h corpus.",
    "Grants pack engages no Security tag and no "
    "accept_default_strict_for<Security>; usually a fresh fixy::fn rewrite.",
    "grant::as_classified  // or as_secret / as_public",
    "fixy::fn<T, /* no Security grant */, ...>");

CRUCIBLE_DEFINE_INSIGHTS_QV(
    ::crucible::fixy::diag::FixyNotEngaged_Protocol,
    ::crucible::safety::diag::Severity::Error,
    "Protocol engages session-type / typestate protocol (sessions/* + "
    "Machine<S>).  Strict default is none; explicit grant::protocol<P> "
    "binds a protocol P.  Without engagement, the type system can't "
    "gate Send/Recv/Choice/Offer adherence.",
    "Grants pack omits grant::protocol<P> AND omits "
    "accept_default_strict_for<Protocol>.",
    "grant::protocol<MyProto>",
    "fixy::fn<T, /* no Protocol grant */, ...>");

CRUCIBLE_DEFINE_INSIGHTS_QV(
    ::crucible::fixy::diag::FixyNotEngaged_Lifetime,
    ::crucible::safety::diag::Severity::Error,
    "Lifetime engages scoping discipline (Static / In<Region>).  Strict "
    "default is Static (no scoping); In<Tag> binds to an OwnedRegion.  "
    "Missing engagement leaves scoped-borrow contracts unverifiable.",
    "Grants pack omits grant::lifetime::* tags AND omits "
    "accept_default_strict_for<Lifetime>.",
    "grant::lifetime::Static  // or lifetime::In<RegionTag>",
    "fixy::fn<T, /* no Lifetime grant */, ...>");

CRUCIBLE_DEFINE_INSIGHTS_QV(
    ::crucible::fixy::diag::FixyNotEngaged_Provenance,
    ::crucible::safety::diag::Severity::Error,
    "Provenance engages the source-of-origin trust boundary tag "
    "(source::* in safety/Tagged.h).  Strict default is source::Internal; "
    "explicit grant::from_source<S> binds external/sanitized provenance.  "
    "Missing engagement defeats trust-boundary review.",
    "Grants pack omits grant::from_source<S> AND omits "
    "accept_default_strict_for<Provenance>.",
    "grant::from_source<source::External>",
    "fixy::fn<T, /* no Provenance grant */, ...>");

CRUCIBLE_DEFINE_INSIGHTS_QV(
    ::crucible::fixy::diag::FixyNotEngaged_Trust,
    ::crucible::safety::diag::Severity::Error,
    "Trust engages review-level (Verified / Tested / Assumed).  Strict "
    "default is Verified; downgrades signal review-debt the §6.8 rules "
    "can dispatch on.  Missing engagement hides review status from "
    "automated audits.",
    "Grants pack omits grant::trust::* tags AND omits "
    "accept_default_strict_for<Trust>.",
    "grant::trust::Verified  // or trust::Tested / trust::Assumed",
    "fixy::fn<T, /* no Trust grant */, ...>");

CRUCIBLE_DEFINE_INSIGHTS_QV(
    ::crucible::fixy::diag::FixyNotEngaged_Representation,
    ::crucible::safety::diag::Severity::Error,
    "Representation engages memory-layout discipline (None / Pinned / "
    "PackedSoA / etc.).  Strict default is None; explicit relaxation "
    "must be cited when layout assumptions are load-bearing (e.g., "
    "Pinned for ABI boundary).  Missing engagement allows layout drift.",
    "Grants pack omits grant::representation::* AND omits "
    "accept_default_strict_for<Representation>.",
    "grant::accept_default_strict_for<dim::DimensionAxis::Representation>",
    "fixy::fn<T, /* no Representation grant */, ...>");

CRUCIBLE_DEFINE_INSIGHTS_QV(
    ::crucible::fixy::diag::FixyNotEngaged_Observability,
    ::crucible::safety::diag::Severity::Error,
    "Observability is the dual of Effect — accept-only, half-engaged "
    "axis (fixy-M-08).  Default.h marks it `HasDerivedDefault` "
    "(strict default RESOLVES through Effect), BUT engagement is "
    "still required per-axis-uniform — the marker MUST appear in the "
    "Grants pack to declare 'I read the Effect engagement and it "
    "suffices for Observability.'  See fixy-A4-026 structural witness "
    "in Reject.h for the consteval proof that this diagnostic is "
    "alive (the engagement walk has no derived-axis short-circuit).",
    "Grants pack omits accept_default_strict_for<Observability> (the "
    "only legal engagement for this axis); 'Observability is derived "
    "so I don't need a marker' is the canonical author trap.",
    "grant::accept_default_strict_for<dim::DimensionAxis::Observability>",
    "fixy::fn<T, /* no Observability grant */, ...>");

CRUCIBLE_DEFINE_INSIGHTS_QV(
    ::crucible::fixy::diag::FixyNotEngaged_Complexity,
    ::crucible::safety::diag::Severity::Error,
    "Complexity engages cost-class annotation (O(1) / O(N) / O(N²) / "
    "O(NlogN)).  Strict default is None — but bench-gated review on "
    "hot paths reads this axis for cost-budget rollup.  Missing "
    "engagement breaks the cost-model lattice.",
    "Grants pack omits grant::complexity::* AND omits "
    "accept_default_strict_for<Complexity>.",
    "grant::accept_default_strict_for<dim::DimensionAxis::Complexity>",
    "fixy::fn<T, /* no Complexity grant */, ...>");

CRUCIBLE_DEFINE_INSIGHTS_QV(
    ::crucible::fixy::diag::FixyNotEngaged_Precision,
    ::crucible::safety::diag::Severity::Error,
    "Precision engages FP error-bound tier (BITEXACT_STRICT / BITEXACT_TC / "
    "ORDERED / UNORDERED).  Strict default is BITEXACT_STRICT; relaxation "
    "must be cited.  Missing engagement defeats cross-vendor numerics CI "
    "(MIMIC.md §41).",
    "Grants pack omits grant::precision::* AND omits "
    "accept_default_strict_for<Precision>.",
    "grant::accept_default_strict_for<dim::DimensionAxis::Precision>",
    "fixy::fn<T, /* no Precision grant */, ...>");

CRUCIBLE_DEFINE_INSIGHTS_QV(
    ::crucible::fixy::diag::FixyNotEngaged_Space,
    ::crucible::safety::diag::Severity::Error,
    "Space engages allocation-footprint bound (Bytes<N> / Unbounded).  "
    "Strict default is Bytes<0> (no allocation).  Hot-path callers gate "
    "on this axis to refuse work that exceeds an arena budget.  Missing "
    "engagement hides allocation behavior from the type system.",
    "Grants pack omits grant::space::* AND omits "
    "accept_default_strict_for<Space>.",
    "grant::accept_default_strict_for<dim::DimensionAxis::Space>",
    "fixy::fn<T, /* no Space grant */, ...>");

CRUCIBLE_DEFINE_INSIGHTS_QV(
    ::crucible::fixy::diag::FixyNotEngaged_Overflow,
    ::crucible::safety::diag::Severity::Error,
    "Overflow engages integer-overflow discipline (Trap / Wrap / Saturate / "
    "Widen).  Strict default is Trap; Wrap/Saturate must be cited.  "
    "Missing engagement leaves §6.8 N002 (decimal × wrap) unable to "
    "fire on the canonical IEEE-decimal × wrap-arithmetic mismatch.",
    "Grants pack omits grant::overflow::* AND omits "
    "accept_default_strict_for<Overflow>.",
    "grant::accept_default_strict_for<dim::DimensionAxis::Overflow>",
    "fixy::fn<T, /* no Overflow grant */, ...>");

CRUCIBLE_DEFINE_INSIGHTS_QV(
    ::crucible::fixy::diag::FixyNotEngaged_Mutation,
    ::crucible::safety::diag::Severity::Error,
    "Mutation engages write-discipline (Immutable / Append / Monotonic / "
    "Mutable).  Strict default is Immutable.  Append/Monotonic must be "
    "cited so §6.8 M012 (monotonic × concurrent) can gate concurrent "
    "writes; missing engagement breaks lattice-monotone publication.",
    "Grants pack omits grant::mutation::* AND omits "
    "accept_default_strict_for<Mutation>.",
    "grant::accept_default_strict_for<dim::DimensionAxis::Mutation>",
    "fixy::fn<T, /* no Mutation grant */, ...>");

CRUCIBLE_DEFINE_INSIGHTS_QV(
    ::crucible::fixy::diag::FixyNotEngaged_Reentrancy,
    ::crucible::safety::diag::Severity::Error,
    "Reentrancy engages re-entry safety (NonReentrant / Reentrant / "
    "Coroutine).  Strict default is NonReentrant.  Coroutine and "
    "Reentrant must be cited so collision rules dispatch on async-vs-"
    "borrow patterns (§6.8 L002 / E044).",
    "Grants pack omits grant::reentrancy::* AND omits "
    "accept_default_strict_for<Reentrancy>.",
    "grant::accept_default_strict_for<dim::DimensionAxis::Reentrancy>",
    "fixy::fn<T, /* no Reentrancy grant */, ...>");

CRUCIBLE_DEFINE_INSIGHTS_QV(
    ::crucible::fixy::diag::FixyNotEngaged_Size,
    ::crucible::safety::diag::Severity::Error,
    "Size engages codata observation-depth (Bounded<N> / Unbounded).  "
    "Strict default is Bounded<1> (one observation).  Streaming / "
    "infinite-codata authors cite Unbounded; missing engagement hides "
    "the productivity discipline from the lattice.",
    "Grants pack omits grant::size::* AND omits "
    "accept_default_strict_for<Size>.",
    "grant::accept_default_strict_for<dim::DimensionAxis::Size>",
    "fixy::fn<T, /* no Size grant */, ...>");

CRUCIBLE_DEFINE_INSIGHTS_QV(
    ::crucible::fixy::diag::FixyNotEngaged_Version,
    ::crucible::safety::diag::Severity::Error,
    "Version engages schema-version number for serialized data shapes.  "
    "Strict default is V0; explicit grant::version<N> binds an evolved "
    "schema.  Missing engagement defeats federation-cache row_hash "
    "fragmentation defense (GAPS-028).",
    "Grants pack omits grant::version::* AND omits "
    "accept_default_strict_for<Version>.",
    "grant::accept_default_strict_for<dim::DimensionAxis::Version>",
    "fixy::fn<T, /* no Version grant */, ...>");

CRUCIBLE_DEFINE_INSIGHTS_QV(
    ::crucible::fixy::diag::FixyNotEngaged_Staleness,
    ::crucible::safety::diag::Severity::Error,
    "Staleness engages freshness-bound τ (Fresh / Stale<τ>).  Strict "
    "default is Fresh.  Cache/replay paths cite Stale<τ> so §30.14 "
    "corpus 'staleness × secret without declassify' can detect leak "
    "patterns.  Missing engagement bypasses freshness audit.",
    "Grants pack omits grant::stale_to<TauMax> AND omits "
    "accept_default_strict_for<Staleness>.",
    "grant::accept_default_strict_for<dim::DimensionAxis::Staleness>",
    "fixy::fn<T, /* no Staleness grant */, ...>");

// ═══════════════════════════════════════════════════════════════════
// ── 4 §30.14 corpus entry specializations ─────────────────────────
// ═══════════════════════════════════════════════════════════════════

CRUCIBLE_DEFINE_INSIGHTS_QV(
    ::crucible::fixy::theory::corpus::classified_io_without_declassify,
    ::crucible::safety::diag::Severity::Fatal,
    "Volpano-Smith-Irvine 1996 + Sabelfeld-Myers 2003 implicit information "
    "flow: a classified value reaches an I/O boundary without an audit-"
    "discharging declassification policy.  Sequential IFC type systems "
    "require an explicit policy at every classified→IO transition.",
    "Binding engages as_secret/as_classified on Security AND with<...,IO,...> "
    "on Effect AND omits any grant::declassify<Policy>.",
    "grant::declassify<secret_policy::AuthorizedExport>",
    "fixy::fn<T, as_secret, grant::with<effects::Effect::IO>>  // no declassify");

CRUCIBLE_DEFINE_INSIGHTS_QV(
    ::crucible::fixy::theory::corpus::classified_bg_without_declassify,
    ::crucible::safety::diag::Severity::Fatal,
    "Smith-Volpano 1998 + Sabelfeld-Sands 2000 + Hedin-Sabelfeld 2012 "
    "concurrent information flow: a classified value crosses into a "
    "background-thread context whose scheduling becomes secret-dependent.  "
    "Sequential IFC is UNSOUND under concurrency; declassify must fire.",
    "Binding engages as_secret/as_classified on Security AND "
    "with<...,Bg,...> on Effect AND omits any grant::declassify<Policy>.",
    "grant::declassify<secret_policy::CrossThreadAuthorized>",
    "fixy::fn<T, as_secret, grant::with<effects::Effect::Bg>>  // no declassify");

// fixy-A4-004: why_this_matters sourced directly from corpus::cite() to
// eliminate the parallel-header citation drift class (Theory.h hardened
// under fixy-H-17 to demote SS09; this Insights.h surface previously
// shipped only the demoted survey cite).
CRUCIBLE_DEFINE_INSIGHTS_QV(
    ::crucible::fixy::theory::corpus::staleness_secret_without_declassify,
    ::crucible::safety::diag::Severity::Fatal,
    ::crucible::fixy::theory::corpus::staleness_secret_without_declassify::cite(),
    "Binding engages as_secret/as_classified on Security AND grant::stale_to<τ> "
    "on Staleness AND omits any grant::declassify<Policy>.",
    "grant::declassify<secret_policy::FreshnessDischarged>",
    "fixy::fn<T, as_secret, grant::stale_to<100>>  // no declassify");

// fixy-A4-003: why_this_matters sourced directly from corpus::cite() to
// eliminate the parallel-header citation drift class (Theory.h corrected
// under fixy-CR-17 to attribute ghost-state discipline to Filliâtre-
// Gondelman-Paskevich 2014 / Leino 2010, replacing a fabricated MSS-2016
// attribution that this Insights.h surface previously shipped).
CRUCIBLE_DEFINE_INSIGHTS_QV(
    ::crucible::fixy::theory::corpus::ghost_runtime_observable,
    ::crucible::safety::diag::Severity::Fatal,
    ::crucible::fixy::theory::corpus::ghost_runtime_observable::cite(),
    "Binding engages grant::ghost on Usage AND grant::with<...> contains "
    "Alloc, IO, Block, or Bg.",
    "grant::as_linear  // or as_affine — drop ghost",
    "fixy::fn<T, grant::ghost, grant::with<effects::Effect::IO>>");

CRUCIBLE_DEFINE_INSIGHTS_QV(
    ::crucible::fixy::theory::corpus::internal_io_without_declassify,
    ::crucible::safety::diag::Severity::Fatal,
    "Bell-LaPadula 1973 + Volpano-Smith-Irvine 1996 + Sabelfeld-Myers 2003 "
    "no-write-down: org-internal data (SecLevel::Internal, below the strict "
    "default Classified but above Public) flows into an I/O sink without "
    "an audit-discharging declassification policy.  Every non-Public→Public "
    "crossing requires declassify, not just Classified/Secret tiers.",
    "Binding engages grant::as_internal on Security AND with<...,IO,...> "
    "on Effect AND omits any grant::declassify<Policy>.",
    "grant::declassify<secret_policy::AuthorizedExport>  // org-disclosure",
    "fixy::fn<T, grant::as_internal, grant::with<effects::Effect::IO>>");

// fixy-A4-008: concurrent dual of internal_io_without_declassify per
// the §30.14 corpus 2-by-2 (Security tier {Secret, Internal} × Effect
// channel {IO, Bg}) closure.  Smith-Volpano 1998 supplies the
// concurrent-IFC discharge — the spawn itself is a scheduler-
// observable event, so sequential type systems are unsound under
// concurrency even when the data-flow IO channel is absent.  cite()
// sourced directly from corpus::internal_bg_without_declassify::cite()
// to eliminate the parallel-header citation drift class (same A4-003
// / A4-004 lesson).
CRUCIBLE_DEFINE_INSIGHTS_QV(
    ::crucible::fixy::theory::corpus::internal_bg_without_declassify,
    ::crucible::safety::diag::Severity::Fatal,
    ::crucible::fixy::theory::corpus::internal_bg_without_declassify::cite(),
    "Binding engages grant::as_internal on Security AND with<...,Bg,...> "
    "on Effect AND omits any grant::declassify<Policy>.",
    "grant::declassify<secret_policy::CrossThreadAuthorized>  // cross-thread",
    "fixy::fn<T, grant::as_internal, grant::with<effects::Effect::Bg>>");
