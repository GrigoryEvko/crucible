#pragma once

// ── crucible::fixy::diag — per-tag insight specializations ─────────
//
// fixy-H-14: the 33 `FixyNotEngaged_<Axis>` tags + the 6 §30.14
// theory-corpus entries each ship an `insight_provider` specialization
// (FIXY-FOUND-127: previously "20 / 4"; now corrected — see counts
// below.  The 22-axis prose at line 23 was equally stale.)
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
//   fixy/Reject.h::FixyNotEngaged_*  — the 33 axis-engagement tags
//                                      (FIXY-FOUND-127: was 22).
//   fixy/Theory.h::corpus::*         — the 6 §30.14 unsoundness tags
//                                      (FIXY-FOUND-127: was 4).
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
// ── 33 FixyNotEngaged_<Axis> specializations (FIXY-U-110 + sweep) ──
// ═══════════════════════════════════════════════════════════════════
// 20 original axes + Synchronization (fixy-A3-008, 2026-05-18) +
// Regime (fixy-A3-009, 2026-05-18) + 11 added since (FpMode V-088,
// SyscallSurface V-097, ControlFlow / CallShape / StackUse /
// GlobalState / Stdio V-238, HwInstruction / BarrierStrength /
// SimdIsa V-253, MemoryScope V-266) — FIXY-FOUND-127 corrects the
// stale "22" banner that pre-dated those 11 additions.  Coverage is
// reflection-witnessed at the bottom of this header: every
// DimensionAxis enumerator must have a corresponding
// insight_provider specialization, or the coverage sentinel fails
// to compile.

CRUCIBLE_DEFINE_INSIGHTS_QV(
    ::crucible::fixy::diag::FixyNotEngaged_Type,
    ::crucible::safety::diag::Severity::Error,
    "The Type axis names the function/callable's principal type and is the "
    "anchor for §6.8 collision rules in safety/CollisionCatalog.h.  "
    "fixy::fn<T, ...> implicitly mints the Type marker (FIXY-AUDIT-A7); "
    "this diagnostic fires when IsAccepted is invoked directly without it.",
    "User wrote IsAccepted<Grants...> at namespace scope with no Type marker "
    "passed (also no fixy::fn<> wrapper to inject one).",
    // FIXY-FOUND-128: previous correct_example shipped `grant::with<>, ...`
    // which reads as if Effect engagement alone satisfies the gate.  It does
    // not — 32 other axes still need engagement, so a literal copy-paste
    // tier-3 reds on Refinement/Usage/Security/Lifetime/...  Use a canonical
    // pre-built stance pack to demonstrate a clean-compile fixy::fn shape;
    // stance::RealtimeHot ships a documented complete grants pack
    // (Fn.h:1804-1820 self-tests it).
    "fixy::fn<MyCallable, stance::RealtimeHot<MyCallable>>(MyCallable{});",
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

// FIXY-FOUND-129: prior doc-block cited "§6.8 L002 / E044" as the
// Reentrancy-axis collision rules.  Both citations are wrong: L002
// (borrow × Async) dispatches on Lifetime × Synchronization, and E044
// (CT × Async) dispatches on Security × Synchronization.  Neither
// reads the Reentrancy grade.  The Reentrancy axis currently ships
// with ZERO dispatching collision rules (FIXY-FOUND-071 tracks the
// R001/R002/R003 family that would close the gap); engagement here
// is therefore a SCOPE-LEVEL audit-trail discipline rather than a
// concept-gate prerequisite for any active rule.
CRUCIBLE_DEFINE_INSIGHTS_QV(
    ::crucible::fixy::diag::FixyNotEngaged_Reentrancy,
    ::crucible::safety::diag::Severity::Error,
    "Reentrancy engages re-entry safety (NonReentrant / Reentrant / "
    "Coroutine).  Strict default is NonReentrant.  Engagement is "
    "currently SCOPE-LEVEL audit-trail only — no §6.8 collision rule "
    "dispatches on the Reentrancy grade today (FIXY-FOUND-071 tracks "
    "the planned R001/R002/R003 family).  Cite explicitly so a future "
    "rule family lands on a binding that already declares its position.",
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

// FIXY-U-110: Synchronization axis added 2026-05-18 per fixy-A3-008.
// Strict default is sync::Unconstrained — the binding makes no claim
// about wait strategy / memory order at the binding scope; the
// Wait<Strategy, T> / MemOrder<Tag, T> value wrapper carries the
// discipline.  Missing engagement leaves the §6.8 collision catalog
// unable to gate on E044 (Wait × CT incompatible) or S010 (Staleness
// × CT incompatible) — both rules dispatch on the Wait/MemOrder grade.
CRUCIBLE_DEFINE_INSIGHTS_QV(
    ::crucible::fixy::diag::FixyNotEngaged_Synchronization,
    ::crucible::safety::diag::Severity::Error,
    "Synchronization engages wait-strategy + memory-order discipline "
    "(safety::Wait<Strategy, T> + safety::MemOrder<Tag, T>).  Strict "
    "default is sync::Unconstrained — the binding makes no scope-level "
    "claim; wrapped values carry per-value discipline.  Missing "
    "engagement defeats the §6.8 E044 (Wait × CT) collision rule and "
    "the constant-time × non-fresh-staleness audit (S010); both rules "
    "dispatch on the Synchronization grade.",
    "Grants pack omits grant::with_sync<Strategy> AND omits "
    "accept_default_strict_for<Synchronization>.",
    "grant::accept_default_strict_for<dim::DimensionAxis::Synchronization>",
    "fixy::fn<T, /* no Synchronization grant */, ...>");

// FIXY-U-110: Regime axis added 2026-05-18 per fixy-A3-009.  Strict
// default is regime::Unconstrained — the binding makes no claim about
// operating regime at scope, deferring to the HotPath<Tier, T> value
// wrapper (which itself defaults to Cold).
//
// FIXY-FOUND-125: the prior doc-block claimed Regime engagement drove
// H001/H002/H003 dispatch.  That is FALSE.  Today the §6.8 H001/H002/
// H003 family (HotPath × unbounded-cost; HotPath × pred::True; HotPath
// × Alloc-or-IO-with-unbounded) dispatches on the `marks_hot_path<F>`
// marker trait (CollisionCatalog.h:1530-1544), which has ZERO `:
// std::true_type` specializations in the tree.  No grant — including a
// (still-unimplemented) `grant::with_regime<Hot>` — opts a binding into
// `marks_hot_path = true_type`; the marker is dormant, tracked under
// FIXY-FOUND-067 ("21 of 47 collision rules dormant — markers default
// false, no production specialization").  The Regime axis is therefore
// a SCOPE-level engagement audit with no current concept-gate consumer;
// its sole engagement path is `accept_default_strict_for<Regime>`
// (positive `grant::with_regime<Tier>` is unimplemented).
CRUCIBLE_DEFINE_INSIGHTS_QV(
    ::crucible::fixy::diag::FixyNotEngaged_Regime,
    ::crucible::safety::diag::Severity::Error,
    "Regime engages operating-tier discipline at the SCOPE level "
    "(grants-pack), parallel to the safety::HotPath<Tier, T> VALUE "
    "wrapper.  Strict default is regime::Unconstrained — the binding "
    "defers to the value wrapper, which itself defaults to Cold.  "
    "Engagement is an audit-trail discipline: the §6.8 H001/H002/H003 "
    "collision family currently dispatches on the marks_hot_path<F> "
    "marker trait (NOT on the Regime grade — FIXY-FOUND-125 correction), "
    "and that marker is dormant pending FIXY-FOUND-067.  When a positive "
    "grant::with_regime<Hot> ships, it is expected to specialize "
    "marks_hot_path<F> = true_type for the binding's F type, at which "
    "point H001/H002/H003 will fire as documented.",
    "Grants pack omits accept_default_strict_for<Regime> (no positive "
    "grant::with_regime<Tier> ships today; engagement is opt-out only).",
    "grant::accept_default_strict_for<dim::DimensionAxis::Regime>",
    "fixy::fn<T, /* no Regime grant */, ...>");

// FIXY-V-088: FpMode axis added 2026-05-22.  Strict default is
// fp::Strict (every sub-axis pinned to the most-IEEE-compliant element)
// — bindings make no claim about FP evaluation policy at scope,
// deferring to safety::Fp* value wrappers (V-090 ships them).  Missing
// engagement disables the §6.8 F101-F105 collision family (FpMode ×
// Precision, FpMode × Vendor, FpMode × NumericalRecipe, FpMode ×
// DetSafe, FpMode × HotPath) shipped by V-091 (FIXY-FOUND-126:
// previously "once V-091 lands"; V-091's rules are now in the
// catalog).
CRUCIBLE_DEFINE_INSIGHTS_QV(
    ::crucible::fixy::diag::FixyNotEngaged_FpMode,
    ::crucible::safety::diag::Severity::Error,
    "FpMode engages floating-point evaluation policy (Rounding / Ftz / "
    "Contract / TrapMask / Denormal / NanPolicy / InfPolicy / "
    "ComplexLayout / LibmPolicy / Reassociate / FpConstant — the 11-sub-"
    "axis taxonomy V-089 shipped as ChainLattice algebras and V-090 "
    "shipped as safety/FpMode.h wrappers + FpModeProductLattice "
    "composite; FIXY-FOUND-132-AUDIT made present-tense).  Strict default is "
    "fp::Strict (every sub-axis pinned to the most-IEEE-compliant "
    "element).  Missing engagement defeats the §6.8 F101-F105 collision "
    "family (FpMode × Precision, FpMode × Vendor, FpMode × "
    "NumericalRecipe, FpMode × DetSafe, FpMode × HotPath) which V-091 "
    "ships — those rules dispatch on the FpMode grade.",
    "Grants pack omits grant::with_fp_mode<...> AND omits "
    "accept_default_strict_for<FpMode>.",
    "grant::accept_default_strict_for<dim::DimensionAxis::FpMode>",
    "fixy::fn<T, /* no FpMode grant */, ...>");

// FIXY-V-097: SyscallSurface axis added 2026-05-22.  Strict default is
// syscall::Unconstrained — bindings make no claim about syscall surface
// at this scope, deferring to the syscall::* grant family that V-098
// ships in fixy/syscall/{Family,Per}.h.
//
// FIXY-FOUND-132: prior prose claimed "V-098+ WILL ship" the value
// wrapper and "V-100 WILL ship" the §6.8 S101-S104 collision family.
// Both verbs were stale: V-097 (axis + lattice), V-098 (9 family + per-
// SyscallId grant providers) and V-100 (fixy/syscall/Bridge.h SyscallSurface
// → Met(X) effect-row lift) ARE all shipped today (commits 3da87d7d /
// 8dac697e / 409c652c).  However the dependent S101-S104 collision
// rules that V-100's bridge was intended to feed have NOT landed in
// CollisionCatalog.h — they are tracked-future, no separately-numbered
// FIXY-V-* task yet.  This entry presents the bridge ↔ rule split
// honestly: bridge shipped, dispatching rules dormant.
CRUCIBLE_DEFINE_INSIGHTS_QV(
    ::crucible::fixy::diag::FixyNotEngaged_SyscallSurface,
    ::crucible::safety::diag::Severity::Error,
    "SyscallSurface engages the syscall-family taxonomy (NoSyscall / "
    "VdsoOnly / ReadOnlyState / FileMutation / MemoryMapping / "
    "ThreadSync / NetworkIo / ProcessControl / Privilege — the 9-tier "
    "chain V-097 ships in algebra/lattices/SyscallFamilyLattice.h).  "
    "Strict default is syscall::Unconstrained (the binding defers to "
    "the syscall::* grant family that V-098 ships in fixy/syscall/"
    "{Family,Per}.h, lifted to Met(X) by V-100's fixy/syscall/Bridge.h).  "
    "Engagement is currently AUDIT-TRAIL ONLY: the planned §6.8 "
    "S101-S104 collision family (SyscallSurface × HotPath / DetSafe / "
    "Vendor / Security) is NOT in CollisionCatalog.h today — V-097/"
    "V-098/V-100 shipped the axis + grants + effect-row lift but the "
    "dispatching rules remain tracked-future (FIXY-FOUND-132 audit).  "
    "Cite explicitly so the future rule family lands on a binding that "
    "already declares its syscall-surface position.",
    "Grants pack omits grant::with_syscall<...> AND omits "
    "accept_default_strict_for<SyscallSurface>.",
    "grant::accept_default_strict_for<dim::DimensionAxis::SyscallSurface>",
    "fixy::fn<T, /* no SyscallSurface grant */, ...>");

// FIXY-V-266 follow-on (surfaced by V-273): MemoryScope axis (slot 32)
// added 2026-05-23.  The DimensionAxis enumerator + Reject.h tag wiring
// shipped, but the insight provider was the one unwired piece of the
// universe-tax — only surfaced when a fresh TU recompiled fixy/Insights.h.
// Strict default is scope::Unconstrained — the binding makes no claim
// about memory-visibility scope, deferring to the safety::ScopedFence<
// Scope, T> value wrapper (V-267).  Missing engagement defeats the §6.8
// V401 (scope⊒Gpu ⇒ strength⊒AcqRel) and V402 (scope×arch cross-trunk
// reject) collision rules (V-268), both of which dispatch on the
// MemoryScope grade.
CRUCIBLE_DEFINE_INSIGHTS_QV(
    ::crucible::fixy::diag::FixyNotEngaged_MemoryScope,
    ::crucible::safety::diag::Severity::Error,
    "MemoryScope engages memory-visibility scope discipline "
    "(safety::ScopedFence<Scope, T> — Thread / Warp / Cta / Cluster / Gpu "
    "accel trunk x Inner(ISH) / Outer(OSH) ARM trunk, joined at Thread / "
    "System; a Tier-L non-distributive lattice for scoped-fence + async-"
    "copy publish).  Strict default is scope::Unconstrained — the binding "
    "makes no scope-level claim; the value wrapper carries per-value "
    "visibility.  Missing engagement defeats the §6.8 V401 (scope-supseteq-"
    "Gpu requires strength-supseteq-AcqRel) and V402 (scope x arch cross-"
    "trunk reject) collision rules, both of which dispatch on the "
    "MemoryScope grade.",
    "Grants pack omits grant::hw::scope<Scope, Arch> (or grant::async::"
    "copy<...>) AND omits accept_default_strict_for<MemoryScope>.",
    "grant::accept_default_strict_for<dim::DimensionAxis::MemoryScope>",
    "fixy::fn<T, /* no MemoryScope grant */, ...>");

// FIXY-V-238: ControlFlow / CallShape / StackUse / GlobalState / Stdio
// axes added 2026-05-23.  Each strict default is the matching
// safety::fn::<ns>::Unconstrained — bindings make no claim about that
// behavior at this scope, deferring to V-242's value wrapper if one is
// present.  Missing engagement disables the §6.8 C001 / D001 / D002 /
// G001 / L006 / P003 / S001 / S004 collision family shipped by V-243
// (FIXY-FOUND-126: previously "once it wires up"; V-243's rules now
// live in the catalog) — those rules dispatch on the respective
// behavior grade.
CRUCIBLE_DEFINE_INSIGHTS_QV(
    ::crucible::fixy::diag::FixyNotEngaged_ControlFlow,
    ::crucible::safety::diag::Severity::Error,
    "ControlFlow engages the control-flow taxonomy (Pure ⊏ AbortOnly ⊏ "
    "ThrowOnly ⊏ MayLongjmp ⊏ MaySignal — the 5-tier chain V-239 ships "
    "in algebra/lattices/ControlFlowLattice.h).  Strict default is "
    "control_flow::Unconstrained (the binding defers to the value "
    "wrapper that V-242 will ship).  Missing engagement defeats the "
    "permission_fork no-throw requirement (C001) and Forge hot-path "
    "control-flow admission (already enforced structurally by V-087 for "
    "grant::ctrl::throws; ControlFlow makes it a first-class axis).",
    "Grants pack omits grant::ctrl::* AND omits "
    "accept_default_strict_for<ControlFlow>.",
    "grant::accept_default_strict_for<dim::DimensionAxis::ControlFlow>",
    "fixy::fn<T, /* no ControlFlow grant */, ...>");

CRUCIBLE_DEFINE_INSIGHTS_QV(
    ::crucible::fixy::diag::FixyNotEngaged_CallShape,
    ::crucible::safety::diag::Severity::Error,
    "CallShape engages the call-shape taxonomy (Direct ⊏ "
    "BoundedRecurses<N> ⊏ Indirect ⊏ Virtual ⊏ Unbounded — the 5-tier "
    "chain V-240 ships in algebra/lattices/CallShapeLattice.h).  Strict "
    "default is call_shape::Unconstrained.  Missing engagement defeats "
    "bounded-stack admission (D001) and devirtualization gating (D002) "
    "which V-243 ships — those rules dispatch on the CallShape grade.",
    "Grants pack omits grant::dispatch::* AND omits "
    "accept_default_strict_for<CallShape>.",
    "grant::accept_default_strict_for<dim::DimensionAxis::CallShape>",
    "fixy::fn<T, /* no CallShape grant */, ...>");

CRUCIBLE_DEFINE_INSIGHTS_QV(
    ::crucible::fixy::diag::FixyNotEngaged_StackUse,
    ::crucible::safety::diag::Severity::Error,
    "StackUse engages the stack-frame depth discipline (bounded vs "
    "unbounded stack growth — the chain V-241 ships in "
    "algebra/lattices/StackUseLattice.h).  Strict default is "
    "stack_use::Unconstrained.  Missing engagement defeats the §6.8 "
    "G001 / S001 stack-bound collision rules which V-243 ships.",
    "Grants pack omits grant::stack::* AND omits "
    "accept_default_strict_for<StackUse>.",
    "grant::accept_default_strict_for<dim::DimensionAxis::StackUse>",
    "fixy::fn<T, /* no StackUse grant */, ...>");

CRUCIBLE_DEFINE_INSIGHTS_QV(
    ::crucible::fixy::diag::FixyNotEngaged_GlobalState,
    ::crucible::safety::diag::Severity::Error,
    "GlobalState engages the global-state surface (none / readonly / "
    "thread-local / mutable-global — the chain V-241 ships in "
    "algebra/lattices/GlobalStateLattice.h).  Strict default is "
    "global_state::Unconstrained.  Missing engagement defeats the §6.8 "
    "L006 / S004 Meyers-singleton init-cycle detection (V-248) which "
    "V-243 ships — those rules dispatch on the GlobalState grade.",
    "Grants pack omits grant::global::* AND omits "
    "accept_default_strict_for<GlobalState>.",
    "grant::accept_default_strict_for<dim::DimensionAxis::GlobalState>",
    "fixy::fn<T, /* no GlobalState grant */, ...>");

CRUCIBLE_DEFINE_INSIGHTS_QV(
    ::crucible::fixy::diag::FixyNotEngaged_Stdio,
    ::crucible::safety::diag::Severity::Error,
    "Stdio engages the C stdio surface (none / reads / writes — the "
    "chain V-241 ships in algebra/lattices/StdioLattice.h).  Strict "
    "default is stdio::Unconstrained.  Missing engagement defeats the "
    "§6.8 P003 stdio-IO collision rule which V-243 ships — that "
    "rule dispatches on the Stdio grade.",
    "Grants pack omits grant::stdio::* AND omits "
    "accept_default_strict_for<Stdio>.",
    "grant::accept_default_strict_for<dim::DimensionAxis::Stdio>",
    "fixy::fn<T, /* no Stdio grant */, ...>");

// FIXY-V-253: HwInstruction / BarrierStrength / SimdIsa providers.
CRUCIBLE_DEFINE_INSIGHTS_QV(
    ::crucible::fixy::diag::FixyNotEngaged_HwInstruction,
    ::crucible::safety::diag::Severity::Error,
    "HwInstruction engages the hw-instruction capability tier "
    "(NoneAllowed / Scalar / Vectorizable / NonDeterministicTsc / "
    "PrivilegedMsr — the chain V-251 ships in "
    "algebra/lattices/HwInstructionLattice.h).  Strict default is "
    "hw_instruction::Unconstrained.  Missing engagement defeats the "
    "§6.8 H001/H002 rdtsc/rdmsr admission rules which V-260 ships — "
    "those rules dispatch on the HwInstruction grade.",
    "Grants pack omits grant::hw::* AND omits "
    "accept_default_strict_for<HwInstruction>.",
    "grant::accept_default_strict_for<dim::DimensionAxis::HwInstruction>",
    "fixy::fn<T, /* no HwInstruction grant */, ...>");

CRUCIBLE_DEFINE_INSIGHTS_QV(
    ::crucible::fixy::diag::FixyNotEngaged_BarrierStrength,
    ::crucible::safety::diag::Severity::Error,
    "BarrierStrength engages the memory-fence strength ladder (None / "
    "CompilerBarrier / AcquireLoad / ReleaseStore / AcqRel / SeqCst / "
    "FullFence — the chain V-252 ships in "
    "algebra/lattices/BarrierStrengthLattice.h).  Strict default is "
    "barrier_strength::Unconstrained.  Missing engagement defeats the "
    "§6.8 B001 explicit-fence rule which V-260 ships — that rule "
    "dispatches on the BarrierStrength grade.",
    "Grants pack omits grant::hw::barrier<...> AND omits "
    "accept_default_strict_for<BarrierStrength>.",
    "grant::accept_default_strict_for<dim::DimensionAxis::BarrierStrength>",
    "fixy::fn<T, /* no BarrierStrength grant */, ...>");

CRUCIBLE_DEFINE_INSIGHTS_QV(
    ::crucible::fixy::diag::FixyNotEngaged_SimdIsa,
    ::crucible::safety::diag::Severity::Error,
    "SimdIsa engages the SIMD ISA family (Scalar / Portable / "
    "SSE2..AVX512 / NEON..SVE — the Tier-L non-distributive x86×ARM "
    "trunk lattice V-250 ships in algebra/lattices/SimdIsaLattice.h).  "
    "Strict default is simd_isa::Unconstrained.  Missing engagement "
    "defeats the §6.8 V001/V002/S001/S002 cross-ISA + width rules which "
    "V-260 ships — those rules dispatch on the SimdIsa grade.",
    "Grants pack omits grant::simd::width<...> AND omits "
    "accept_default_strict_for<SimdIsa>.",
    "grant::accept_default_strict_for<dim::DimensionAxis::SimdIsa>",
    "fixy::fn<T, /* no SimdIsa grant */, ...>");

// ═══════════════════════════════════════════════════════════════════
// ── 6 §30.14 corpus entry specializations (FIXY-FOUND-127: was 4) ──
// ═══════════════════════════════════════════════════════════════════

CRUCIBLE_DEFINE_INSIGHTS_QV(
    ::crucible::fixy::theory::corpus::classified_io_without_declassify,
    ::crucible::safety::diag::Severity::Fatal,
    "Sabelfeld-Myers 2003 (after Volpano-Smith-Irvine 1996 type-system "
    "foundation) implicit information flow: a classified value reaches an "
    "I/O boundary without an audit-discharging declassification policy.  "
    "Sequential IFC type systems require an explicit policy at every "
    "classified→IO transition.",
    "Binding engages as_secret/as_classified on Security AND with<...,IO,...> "
    "on Effect AND omits any grant::declassify<Policy>.",
    "grant::declassify<secret_policy::WireSerialize>  // encrypted-channel IO",
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
    "grant::declassify<secret_policy::AuditedLogging>  // audit-trail discipline",
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
    "on Staleness AND omits any grant::declassify<Policy> whose policy "
    "discharges the Staleness axis (e.g. secret_policy::AuthorizedReplay).",
    "grant::declassify<secret_policy::AuthorizedReplay>",   // fixy-A4-015: axis-specific
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
    "grant::declassify<secret_policy::WireSerialize>  // encrypted org-disclosure",
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
    "grant::declassify<secret_policy::AuditedLogging>  // cross-thread audit",
    "fixy::fn<T, grant::as_internal, grant::with<effects::Effect::Bg>>");

// ═════════════════════════════════════════════════════════════════════
// ── FIXY-FOUND-016 sentinel — correct-example policy reachability ──
// ═════════════════════════════════════════════════════════════════════
//
// Pre-016 the four `correct_example` strings above cited fictional
// policies (`secret_policy::AuthorizedExport`, `CrossThreadAuthorized`)
// — neither existed in safety/Secret.h, so a contributor who copy-
// pasted the example would face a "no member named X" compile error
// and lose faith in the diagnostic surface.  The strings are inert
// (`correct_example` is `string_view` data, never instantiated as
// code), so the rot was invisible to every test the project ships.
//
// The fix substitutes real policies — `WireSerialize` for IO/disclosure
// channels, `AuditedLogging` for cross-thread audit — both pulled from
// the existing `secret_policy::*` roster.  This static_assert block
// pins those two types: a future rename / removal reds Insights.h
// directly, locally, with the FOUND-016 tag in the diagnostic so the
// fix path is grep-discoverable.  Pinning at the consumer (not at
// safety/Secret.h where the types live) is deliberate — the consumer
// is the audit boundary; the substrate's existence is enforced via
// its own `NotInherited` discipline, not from here.
//
// Without these pins, a future "clean up unused declassify policies"
// PR could silently delete `WireSerialize` and reintroduce the
// invisible-rot class FOUND-016 closed.
static_assert(
    std::derived_from<::crucible::safety::secret_policy::WireSerialize,
                      ::crucible::safety::secret_policy::secret_policy_base>,
    "FIXY-FOUND-016: secret_policy::WireSerialize must exist and inherit "
    "secret_policy_base — it is cited verbatim in the correct_example "
    "field of classified_io_without_declassify and "
    "internal_io_without_declassify above.  Renaming requires either "
    "(a) updating both correct_example strings in lock-step, or "
    "(b) introducing a substitute policy that fits the IO-export "
    "semantics the examples document.  See safety/Secret.h.");
static_assert(
    std::derived_from<::crucible::safety::secret_policy::AuditedLogging,
                      ::crucible::safety::secret_policy::secret_policy_base>,
    "FIXY-FOUND-016: secret_policy::AuditedLogging must exist and inherit "
    "secret_policy_base — it is cited verbatim in the correct_example "
    "field of classified_bg_without_declassify and "
    "internal_bg_without_declassify above.  Renaming requires either "
    "(a) updating both correct_example strings in lock-step, or "
    "(b) introducing a substitute policy that fits the cross-thread "
    "audit-trail semantics the examples document.  See safety/Secret.h.");

// ═════════════════════════════════════════════════════════════════════
// ── FIXY-U-110 coverage sentinel — reflection-driven ───────────────
// ═════════════════════════════════════════════════════════════════════
//
// Closes the catalog-completeness gap: every DimensionAxis enumerator
// must have a corresponding `insight_provider<FixyNotEngaged_<Axis>>`
// specialization in this header.  Without the sentinel, adding a new
// DimensionAxis silently ships without diagnostic insight — caller
// sees the bare `FixyNotEngaged_<NewAxis>` tag name, no Why/Symptom/
// Correct/Violating fields.  fixy-A3-008 (Synchronization) and
// fixy-A3-009 (Regime) were exactly this oversight before FIXY-U-110.
//
// Implementation: reflection enumerates DimensionAxis, projects each
// to its tag via `tag_for_axis<D>::type`, then checks `has_insights_v`
// on the resulting tag.  Any axis without an insight provider trips
// the consteval-fold to false, failing the static_assert.
//
// Theory.h corpus entries: enumerated by hand (no enum to reflect
// over).  Each of the 6 corpus entries has its CRUCIBLE_DEFINE_INSIGHTS
// call in this header above; we sentinel them explicitly so a future
// corpus addition without a matching insight provider trips the
// individual static_assert.
//
// Catalog Category coverage: substrate-level, distinct from fixy::
// layer (safety::diag::Category enumerators correspond to substrate
// diagnostic tags like EffectRowMismatch, LinearityViolation, etc.,
// which are unrelated to fixy's FixyNotEngaged_* and corpus tags).
// Insight providers for those tags belong in production headers per
// their use-site, not in fixy/Insights.h — declared out-of-scope here.

namespace crucible::fixy::insights::self_test {

// ── Per-axis sentinel: every DimensionAxis has an insight provider ─
[[nodiscard]] consteval bool every_axis_has_insight_provider() noexcept {
    // Per fixy/Dim.h:138 note: `^^` cannot follow a using-decl in
    // GCC 16, so reach through to the substrate enum directly.
    static constexpr auto enumerators = std::define_static_array(
        std::meta::enumerators_of(^^::crucible::safety::DimensionAxis));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        using tag_t = typename ::crucible::fixy::diag::tag_for_axis<
            ([:en:])>::type;
        if (!::crucible::safety::diag::has_insights_v<tag_t>) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_axis_has_insight_provider(),
    "FIXY-U-110: a DimensionAxis enumerator has no corresponding "
    "insight_provider<FixyNotEngaged_<Axis>> specialization in "
    "fixy/Insights.h.  Add a CRUCIBLE_DEFINE_INSIGHTS_QV(...) call "
    "for the missing axis, or update tag_for_axis<D>::type in "
    "fixy/Reject.h to surface the new tag.");

// ── Per-corpus sentinel: every Theory.h corpus entry has insight ──
//
// Hand-enumerated (no enum to reflect over).  Adding a 7th corpus
// entry without an insight provider trips its own static_assert here.
static_assert(::crucible::safety::diag::has_insights_v<
    ::crucible::fixy::theory::corpus::classified_io_without_declassify>,
    "Theory.h corpus entry classified_io_without_declassify needs "
    "insight_provider specialization in fixy/Insights.h.");
static_assert(::crucible::safety::diag::has_insights_v<
    ::crucible::fixy::theory::corpus::classified_bg_without_declassify>,
    "Theory.h corpus entry classified_bg_without_declassify needs "
    "insight_provider specialization in fixy/Insights.h.");
static_assert(::crucible::safety::diag::has_insights_v<
    ::crucible::fixy::theory::corpus::staleness_secret_without_declassify>,
    "Theory.h corpus entry staleness_secret_without_declassify needs "
    "insight_provider specialization in fixy/Insights.h.");
static_assert(::crucible::safety::diag::has_insights_v<
    ::crucible::fixy::theory::corpus::ghost_runtime_observable>,
    "Theory.h corpus entry ghost_runtime_observable needs "
    "insight_provider specialization in fixy/Insights.h.");
static_assert(::crucible::safety::diag::has_insights_v<
    ::crucible::fixy::theory::corpus::internal_io_without_declassify>,
    "Theory.h corpus entry internal_io_without_declassify needs "
    "insight_provider specialization in fixy/Insights.h.");
static_assert(::crucible::safety::diag::has_insights_v<
    ::crucible::fixy::theory::corpus::internal_bg_without_declassify>,
    "Theory.h corpus entry internal_bg_without_declassify needs "
    "insight_provider specialization in fixy/Insights.h.");

}  // namespace crucible::fixy::insights::self_test
