#pragma once

#include <crucible/fixy/Reject.h>
#include <crucible/fixy/Theory.h>
#include <crucible/safety/diag/_Insights.h>

CRUCIBLE_DEFINE_INSIGHTS_QV(::crucible::fixy::diag::FixyNotEngaged_Type, ::crucible::safety::diag::Severity::Error,
                            "The Type axis names the function/callable's principal type and is the "
                            "anchor for §6.8 collision rules in safety/CollisionCatalog.h.  "
                            "fixy::fn<T, ...> implicitly mints the Type marker (FIXY-AUDIT-A7); the "
                            "wrapper-tier alias fixy::IsAccepted<T, Grants...> ALSO auto-injects via "
                            "ImplicitTypeMarker (Reject.h:1468-1469) so it can never surface this "
                            "diagnostic — the only path that fires FixyNotEngaged_Type is the "
                            "low-level fixy::IsAcceptedDirect<T, Grants...> with no explicit Type "
                            "engagement marker in the Grants pack.",
                            "User invoked IsAcceptedDirect<MyCallable, Grants...> (the low-level "
                            "gate that bypasses ImplicitTypeMarker auto-injection) with no Type "
                            "engagement marker in Grants — most often a refactor that bypassed the "
                            "fixy::fn wrapper or used `IsAcceptedDirect` for a hand-rolled gate.",
                            "fixy::fn<MyCallable, stance::RealtimeHot<MyCallable>>(MyCallable{});",
                            "static_assert(IsAcceptedDirect<MyCallable>);  // no Type marker → fires _Type");

CRUCIBLE_DEFINE_INSIGHTS_QV(::crucible::fixy::diag::FixyNotEngaged_Refinement,
                            ::crucible::safety::diag::Severity::Error,
                            "Refinement engages value-level predicates (Refined<P, T> family).  "
                            "Strict default is the identity predicate is_no_op_pred; explicit "
                            "grant::refined_with<P> tightens.  Missing engagement means the gate "
                            "can't tell whether the author intended no refinement or forgot.",
                            "Grants pack omits both grant::accept_default_strict_for<Refinement> "
                            "and grant::refined_with<P>.",
                            "grant::accept_default_strict_for<dim::DimensionAxis::Refinement>",
                            "fixy::fn<T, /* no Refinement grant */, ...>");

CRUCIBLE_DEFINE_INSIGHTS_QV(::crucible::fixy::diag::FixyNotEngaged_Usage, ::crucible::safety::diag::Severity::Error,
                            "Usage encodes ownership discipline (Linear / Affine / Copy / Ghost).  "
                            "CSL frame-rule soundness depends on a declared mode; the strict "
                            "default Linear consumes by move, Copy/Affine relax.  Without "
                            "engagement, §6.8 L004/L002/L003 collision rules can't gate.",
                            "Grants pack omits grant::as_linear / as_affine / as_copy / ghost "
                            "AND omits accept_default_strict_for<Usage>.",
                            "grant::as_linear  // or as_copy / as_affine / ghost",
                            "fixy::fn<T, /* no Usage grant */, ...>");

CRUCIBLE_DEFINE_INSIGHTS_QV(::crucible::fixy::diag::FixyNotEngaged_Effect, ::crucible::safety::diag::Severity::Error,
                            "Effect declares the Met(X) row of OS-effects (Alloc/IO/Block/Bg/Init/"
                            "Test).  Strict default is empty Row<>.  Missing engagement leaves "
                            "Subrow admission gates unable to verify ctx-fit at call sites; "
                            "production deps assume the row is declared somewhere.",
                            "Grants pack omits grant::with<...> AND omits "
                            "accept_default_strict_for<Effect>.",
                            "grant::with<effects::Effect::IO>", "fixy::fn<T, /* no Effect grant */, ...>");

CRUCIBLE_DEFINE_INSIGHTS_QV(::crucible::fixy::diag::FixyNotEngaged_Security, ::crucible::safety::diag::Severity::Error,
                            "Security engages information-flow level (Public/Classified/Secret).  "
                            "Strict default is Classified per fixy-CR-01 + 16_05_2026_fixy.md §3 — "
                            "this is load-bearing for §30.14 theory-corpus IFC detection.  Without "
                            "engagement, classified flow into IO/Bg slips past Theory.h corpus.",
                            "Grants pack engages no Security tag and no "
                            "accept_default_strict_for<Security>; usually a fresh fixy::fn rewrite.",
                            "grant::as_classified  // or as_secret / as_public",
                            "fixy::fn<T, /* no Security grant */, ...>");

CRUCIBLE_DEFINE_INSIGHTS_QV(::crucible::fixy::diag::FixyNotEngaged_Protocol, ::crucible::safety::diag::Severity::Error,
                            "Protocol engages session-type / typestate protocol (sessions/* + "
                            "Machine<S>).  Strict default is none; explicit grant::protocol<P> "
                            "binds a protocol P.  Without engagement, the type system can't "
                            "gate Send/Recv/Choice/Offer adherence.",
                            "Grants pack omits grant::protocol<P> AND omits "
                            "accept_default_strict_for<Protocol>.",
                            "grant::protocol<MyProto>", "fixy::fn<T, /* no Protocol grant */, ...>");

CRUCIBLE_DEFINE_INSIGHTS_QV(::crucible::fixy::diag::FixyNotEngaged_Lifetime, ::crucible::safety::diag::Severity::Error,
                            "Lifetime engages scoping discipline (Static / In<Region>).  Strict "
                            "default is Static (no scoping); In<Tag> binds to an OwnedRegion.  "
                            "Missing engagement leaves scoped-borrow contracts unverifiable.",
                            "Grants pack omits grant::lifetime::* tags AND omits "
                            "accept_default_strict_for<Lifetime>.",
                            "grant::lifetime::Static  // or lifetime::In<RegionTag>",
                            "fixy::fn<T, /* no Lifetime grant */, ...>");

CRUCIBLE_DEFINE_INSIGHTS_QV(::crucible::fixy::diag::FixyNotEngaged_Provenance,
                            ::crucible::safety::diag::Severity::Error,
                            "Provenance engages the source-of-origin trust boundary tag "
                            "(source::* in safety/Tagged.h).  Strict default is source::Internal; "
                            "explicit grant::from_source<S> binds external/sanitized provenance.  "
                            "Missing engagement defeats trust-boundary review.",
                            "Grants pack omits grant::from_source<S> AND omits "
                            "accept_default_strict_for<Provenance>.",
                            "grant::from_source<source::External>", "fixy::fn<T, /* no Provenance grant */, ...>");

CRUCIBLE_DEFINE_INSIGHTS_QV(::crucible::fixy::diag::FixyNotEngaged_Trust, ::crucible::safety::diag::Severity::Error,
                            "Trust engages review-level (Verified / Tested / Assumed).  Strict "
                            "default is Verified; downgrades signal review-debt the §6.8 rules "
                            "can dispatch on.  Missing engagement hides review status from "
                            "automated audits.",
                            "Grants pack omits grant::trust::* tags AND omits "
                            "accept_default_strict_for<Trust>.",
                            "grant::trust::Verified  // or trust::Tested / trust::Assumed",
                            "fixy::fn<T, /* no Trust grant */, ...>");

CRUCIBLE_DEFINE_INSIGHTS_QV(::crucible::fixy::diag::FixyNotEngaged_Representation,
                            ::crucible::safety::diag::Severity::Error,
                            "Representation engages memory-layout discipline (None / Pinned / "
                            "PackedSoA / etc.).  Strict default is None; explicit relaxation "
                            "must be cited when layout assumptions are load-bearing (e.g., "
                            "Pinned for ABI boundary).  Missing engagement allows layout drift.",
                            "Grants pack omits grant::representation::* AND omits "
                            "accept_default_strict_for<Representation>.",
                            "grant::accept_default_strict_for<dim::DimensionAxis::Representation>",
                            "fixy::fn<T, /* no Representation grant */, ...>");

CRUCIBLE_DEFINE_INSIGHTS_QV(::crucible::fixy::diag::FixyNotEngaged_Observability,
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

CRUCIBLE_DEFINE_INSIGHTS_QV(::crucible::fixy::diag::FixyNotEngaged_Complexity,
                            ::crucible::safety::diag::Severity::Error,
                            "Complexity engages cost-class annotation (O(1) / O(N) / O(N²) / "
                            "O(NlogN)).  Strict default is None — but bench-gated review on "
                            "hot paths reads this axis for cost-budget rollup.  Missing "
                            "engagement breaks the cost-model lattice.",
                            "Grants pack omits grant::complexity::* AND omits "
                            "accept_default_strict_for<Complexity>.",
                            "grant::accept_default_strict_for<dim::DimensionAxis::Complexity>",
                            "fixy::fn<T, /* no Complexity grant */, ...>");

CRUCIBLE_DEFINE_INSIGHTS_QV(::crucible::fixy::diag::FixyNotEngaged_Precision, ::crucible::safety::diag::Severity::Error,
                            "Precision engages FP error-bound tier (BITEXACT_STRICT / BITEXACT_TC / "
                            "ORDERED / UNORDERED).  Strict default is BITEXACT_STRICT; relaxation "
                            "must be cited.  Missing engagement defeats cross-vendor numerics CI "
                            "(MIMIC.md §41).",
                            "Grants pack omits grant::precision::* AND omits "
                            "accept_default_strict_for<Precision>.",
                            "grant::accept_default_strict_for<dim::DimensionAxis::Precision>",
                            "fixy::fn<T, /* no Precision grant */, ...>");

CRUCIBLE_DEFINE_INSIGHTS_QV(::crucible::fixy::diag::FixyNotEngaged_Space, ::crucible::safety::diag::Severity::Error,
                            "Space engages allocation-footprint bound (Bytes<N> / Unbounded).  "
                            "Strict default is Bytes<0> (no allocation).  Hot-path callers gate "
                            "on this axis to refuse work that exceeds an arena budget.  Missing "
                            "engagement hides allocation behavior from the type system.",
                            "Grants pack omits grant::space::* AND omits "
                            "accept_default_strict_for<Space>.",
                            "grant::accept_default_strict_for<dim::DimensionAxis::Space>",
                            "fixy::fn<T, /* no Space grant */, ...>");

CRUCIBLE_DEFINE_INSIGHTS_QV(::crucible::fixy::diag::FixyNotEngaged_Overflow, ::crucible::safety::diag::Severity::Error,
                            "Overflow engages integer-overflow discipline (Trap / Wrap / Saturate / "
                            "Widen).  Strict default is Trap; Wrap/Saturate must be cited.  "
                            "Missing engagement leaves §6.8 N002 (decimal × wrap) unable to "
                            "fire on the canonical IEEE-decimal × wrap-arithmetic mismatch.",
                            "Grants pack omits grant::overflow::* AND omits "
                            "accept_default_strict_for<Overflow>.",
                            "grant::accept_default_strict_for<dim::DimensionAxis::Overflow>",
                            "fixy::fn<T, /* no Overflow grant */, ...>");

CRUCIBLE_DEFINE_INSIGHTS_QV(::crucible::fixy::diag::FixyNotEngaged_Mutation, ::crucible::safety::diag::Severity::Error,
                            "Mutation engages write-discipline (Immutable / Append / Monotonic / "
                            "Mutable).  Strict default is Immutable.  Append/Monotonic must be "
                            "cited so §6.8 M012 (monotonic × concurrent) can gate concurrent "
                            "writes; missing engagement breaks lattice-monotone publication.",
                            "Grants pack omits grant::mutation::* AND omits "
                            "accept_default_strict_for<Mutation>.",
                            "grant::accept_default_strict_for<dim::DimensionAxis::Mutation>",
                            "fixy::fn<T, /* no Mutation grant */, ...>");

CRUCIBLE_DEFINE_INSIGHTS_QV(::crucible::fixy::diag::FixyNotEngaged_Reentrancy,
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

CRUCIBLE_DEFINE_INSIGHTS_QV(::crucible::fixy::diag::FixyNotEngaged_Size, ::crucible::safety::diag::Severity::Error,
                            "Size engages codata observation-depth (Bounded<N> / Unbounded).  "
                            "Strict default is Bounded<1> (one observation).  Streaming / "
                            "infinite-codata authors cite Unbounded; missing engagement hides "
                            "the productivity discipline from the lattice.",
                            "Grants pack omits grant::size::* AND omits "
                            "accept_default_strict_for<Size>.",
                            "grant::accept_default_strict_for<dim::DimensionAxis::Size>",
                            "fixy::fn<T, /* no Size grant */, ...>");

CRUCIBLE_DEFINE_INSIGHTS_QV(::crucible::fixy::diag::FixyNotEngaged_Version, ::crucible::safety::diag::Severity::Error,
                            "Version engages schema-version number for serialized data shapes.  "
                            "Strict default is V0; explicit grant::version<N> binds an evolved "
                            "schema.  Missing engagement defeats federation-cache row_hash "
                            "fragmentation defense (GAPS-028).",
                            "Grants pack omits grant::version::* AND omits "
                            "accept_default_strict_for<Version>.",
                            "grant::accept_default_strict_for<dim::DimensionAxis::Version>",
                            "fixy::fn<T, /* no Version grant */, ...>");

CRUCIBLE_DEFINE_INSIGHTS_QV(::crucible::fixy::diag::FixyNotEngaged_Staleness, ::crucible::safety::diag::Severity::Error,
                            "Staleness engages freshness-bound τ (Fresh / Stale<τ>).  Strict "
                            "default is Fresh.  Cache/replay paths cite Stale<τ> so §30.14 "
                            "corpus 'staleness × secret without declassify' can detect leak "
                            "patterns.  Missing engagement bypasses freshness audit.",
                            "Grants pack omits grant::stale_to<TauMax> AND omits "
                            "accept_default_strict_for<Staleness>.",
                            "grant::accept_default_strict_for<dim::DimensionAxis::Staleness>",
                            "fixy::fn<T, /* no Staleness grant */, ...>");

CRUCIBLE_DEFINE_INSIGHTS_QV(::crucible::fixy::diag::FixyNotEngaged_Synchronization,
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

CRUCIBLE_DEFINE_INSIGHTS_QV(::crucible::fixy::diag::FixyNotEngaged_Regime, ::crucible::safety::diag::Severity::Error,
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

CRUCIBLE_DEFINE_INSIGHTS_QV(::crucible::fixy::diag::FixyNotEngaged_FpMode, ::crucible::safety::diag::Severity::Error,
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

CRUCIBLE_DEFINE_INSIGHTS_QV(::crucible::fixy::diag::FixyNotEngaged_SyscallSurface,
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

CRUCIBLE_DEFINE_INSIGHTS_QV(::crucible::fixy::diag::FixyNotEngaged_MemoryScope,
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

CRUCIBLE_DEFINE_INSIGHTS_QV(::crucible::fixy::diag::FixyNotEngaged_ControlFlow,
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

CRUCIBLE_DEFINE_INSIGHTS_QV(::crucible::fixy::diag::FixyNotEngaged_CallShape, ::crucible::safety::diag::Severity::Error,
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

CRUCIBLE_DEFINE_INSIGHTS_QV(::crucible::fixy::diag::FixyNotEngaged_StackUse, ::crucible::safety::diag::Severity::Error,
                            "StackUse engages the stack-frame depth discipline (bounded vs "
                            "unbounded stack growth — the chain V-241 ships in "
                            "algebra/lattices/StackUseLattice.h).  Strict default is "
                            "stack_use::Unconstrained.  Missing engagement defeats the §6.8 "
                            "G001 / S001 stack-bound collision rules which V-243 ships.",
                            "Grants pack omits grant::stack::* AND omits "
                            "accept_default_strict_for<StackUse>.",
                            "grant::accept_default_strict_for<dim::DimensionAxis::StackUse>",
                            "fixy::fn<T, /* no StackUse grant */, ...>");

CRUCIBLE_DEFINE_INSIGHTS_QV(::crucible::fixy::diag::FixyNotEngaged_GlobalState,
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

CRUCIBLE_DEFINE_INSIGHTS_QV(::crucible::fixy::diag::FixyNotEngaged_Stdio, ::crucible::safety::diag::Severity::Error,
                            "Stdio engages the C stdio surface (none / reads / writes — the "
                            "chain V-241 ships in algebra/lattices/StdioLattice.h).  Strict "
                            "default is stdio::Unconstrained.  Missing engagement defeats the "
                            "§6.8 P003 stdio-IO collision rule which V-243 ships — that "
                            "rule dispatches on the Stdio grade.",
                            "Grants pack omits grant::stdio::* AND omits "
                            "accept_default_strict_for<Stdio>.",
                            "grant::accept_default_strict_for<dim::DimensionAxis::Stdio>",
                            "fixy::fn<T, /* no Stdio grant */, ...>");

CRUCIBLE_DEFINE_INSIGHTS_QV(::crucible::fixy::diag::FixyNotEngaged_HwInstruction,
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

CRUCIBLE_DEFINE_INSIGHTS_QV(::crucible::fixy::diag::FixyNotEngaged_BarrierStrength,
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

CRUCIBLE_DEFINE_INSIGHTS_QV(::crucible::fixy::diag::FixyNotEngaged_SimdIsa, ::crucible::safety::diag::Severity::Error,
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

CRUCIBLE_DEFINE_INSIGHTS_QV(::crucible::fixy::theory::corpus::classified_io_without_declassify,
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

CRUCIBLE_DEFINE_INSIGHTS_QV(::crucible::fixy::theory::corpus::classified_bg_without_declassify,
                            ::crucible::safety::diag::Severity::Fatal,
                            "Smith-Volpano 1998 + Sabelfeld-Sands 2000 + Hedin-Sabelfeld 2012 "
                            "concurrent information flow: a classified value crosses into a "
                            "background-thread context whose scheduling becomes secret-dependent.  "
                            "Sequential IFC is UNSOUND under concurrency; declassify must fire.",
                            "Binding engages as_secret/as_classified on Security AND "
                            "with<...,Bg,...> on Effect AND omits any grant::declassify<Policy>.",
                            "grant::declassify<secret_policy::AuditedLogging>  // audit-trail discipline",
                            "fixy::fn<T, as_secret, grant::with<effects::Effect::Bg>>  // no declassify");

// Entries whose why_this_matters reads `cite()` take the citation from the tag
// itself, so the two surfaces cannot drift apart.
CRUCIBLE_DEFINE_INSIGHTS_QV(::crucible::fixy::theory::corpus::staleness_secret_without_declassify,
                            ::crucible::safety::diag::Severity::Fatal,
                            ::crucible::fixy::theory::corpus::staleness_secret_without_declassify::cite(),
                            "Binding engages as_secret/as_classified on Security AND grant::stale_to<τ> "
                            "on Staleness AND omits any grant::declassify<Policy> whose policy "
                            "discharges the Staleness axis (e.g. secret_policy::AuthorizedReplay).",
                            "grant::declassify<secret_policy::AuthorizedReplay>",
                            "fixy::fn<T, as_secret, grant::stale_to<100>>  // no declassify");

CRUCIBLE_DEFINE_INSIGHTS_QV(::crucible::fixy::theory::corpus::ghost_runtime_observable,
                            ::crucible::safety::diag::Severity::Fatal,
                            ::crucible::fixy::theory::corpus::ghost_runtime_observable::cite(),
                            "Binding engages grant::ghost on Usage AND grant::with<...> contains "
                            "Alloc, IO, Block, or Bg.",
                            "grant::as_linear  // or as_affine — drop ghost",
                            "fixy::fn<T, grant::ghost, grant::with<effects::Effect::IO>>");

CRUCIBLE_DEFINE_INSIGHTS_QV(::crucible::fixy::theory::corpus::internal_io_without_declassify,
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

CRUCIBLE_DEFINE_INSIGHTS_QV(::crucible::fixy::theory::corpus::internal_bg_without_declassify,
                            ::crucible::safety::diag::Severity::Fatal,
                            ::crucible::fixy::theory::corpus::internal_bg_without_declassify::cite(),
                            "Binding engages grant::as_internal on Security AND with<...,Bg,...> "
                            "on Effect AND omits any grant::declassify<Policy>.",
                            "grant::declassify<secret_policy::AuditedLogging>  // cross-thread audit",
                            "fixy::fn<T, grant::as_internal, grant::with<effects::Effect::Bg>>");

// The two policies below are named verbatim inside the inert `correct_example`
// strings above, which no compiler ever checks.  These pins put a rename or a
// removal in front of the reader who has to update those strings.  They live
// here rather than beside the policy definitions because this is the citing
// site, and the citation is what breaks.
static_assert(std::derived_from<::crucible::safety::secret_policy::WireSerialize,
                                ::crucible::safety::secret_policy::secret_policy_base>,
              "secret_policy::WireSerialize must exist and inherit "
              "secret_policy_base — it is cited verbatim in the correct_example "
              "field of classified_io_without_declassify and "
              "internal_io_without_declassify above.  Renaming requires either "
              "(a) updating both correct_example strings in lock-step, or "
              "(b) introducing a substitute policy that fits the IO-export "
              "semantics the examples document.");
static_assert(std::derived_from<::crucible::safety::secret_policy::AuditedLogging,
                                ::crucible::safety::secret_policy::secret_policy_base>,
              "secret_policy::AuditedLogging must exist and inherit "
              "secret_policy_base — it is cited verbatim in the correct_example "
              "field of classified_bg_without_declassify and "
              "internal_bg_without_declassify above.  Renaming requires either "
              "(a) updating both correct_example strings in lock-step, or "
              "(b) introducing a substitute policy that fits the cross-thread "
              "audit-trail semantics the examples document.");

namespace crucible::fixy::insights::self_test {

[[nodiscard]] consteval bool every_axis_has_insight_provider() noexcept {
    // `^^` cannot follow a using-declaration in GCC 16, so this names the
    // substrate enum through its full path.
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^::crucible::safety::DimensionAxis));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        using tag_t = typename ::crucible::fixy::diag::tag_for_axis<([:en:])>::type;
        if (!::crucible::safety::diag::has_insights_v<tag_t>) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_axis_has_insight_provider(), "A DimensionAxis enumerator has no corresponding "
                                                 "insight_provider<FixyNotEngaged_<Axis>> specialization.  Add a "
                                                 "CRUCIBLE_DEFINE_INSIGHTS_QV(...) call for the missing axis, or "
                                                 "extend tag_for_axis<D>::type to surface the new tag.");

// The corpus entries are types, not enumerators, so there is nothing to reflect
// over and each one is pinned by hand.
static_assert(
    ::crucible::safety::diag::has_insights_v<::crucible::fixy::theory::corpus::classified_io_without_declassify>,
    "Corpus entry classified_io_without_declassify needs "
    "an insight_provider specialization here.");
static_assert(
    ::crucible::safety::diag::has_insights_v<::crucible::fixy::theory::corpus::classified_bg_without_declassify>,
    "Corpus entry classified_bg_without_declassify needs "
    "an insight_provider specialization here.");
static_assert(
    ::crucible::safety::diag::has_insights_v<::crucible::fixy::theory::corpus::staleness_secret_without_declassify>,
    "Corpus entry staleness_secret_without_declassify needs "
    "an insight_provider specialization here.");
static_assert(::crucible::safety::diag::has_insights_v<::crucible::fixy::theory::corpus::ghost_runtime_observable>,
              "Corpus entry ghost_runtime_observable needs "
              "an insight_provider specialization here.");
static_assert(
    ::crucible::safety::diag::has_insights_v<::crucible::fixy::theory::corpus::internal_io_without_declassify>,
    "Corpus entry internal_io_without_declassify needs "
    "an insight_provider specialization here.");
static_assert(
    ::crucible::safety::diag::has_insights_v<::crucible::fixy::theory::corpus::internal_bg_without_declassify>,
    "Corpus entry internal_bg_without_declassify needs "
    "an insight_provider specialization here.");

}  // namespace crucible::fixy::insights::self_test
