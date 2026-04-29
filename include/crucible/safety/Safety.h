#pragma once

// ── crucible::safety umbrella — value-level wrappers only ───────────
//
// This umbrella aggregates the OBJECT-LEVEL invariant wrappers — the
// primitives that decorate a value with a compile-time predicate /
// classification / linearity / typestate / mutation discipline.
//
// Other substrates have their own umbrellas:
//   <crucible/permissions/Permissions.h>  — CSL ownership family
//   <crucible/handles/Handles.h>          — RAII resource handles
//   <crucible/sessions/Sessions.h>        — protocol-level session types
//   <crucible/bridges/Bridges.h>          — cross-substrate composition
//   <crucible/algebra/Algebra.h>          — Graded<>+lattice foundation
//   <crucible/effects/Effects.h>          — Met(X) effect rows
//
// See code_guide.md §XVI for axiom mapping, usage rules, compiler
// enforcement, and review enforcement rules for each wrapper.
// 25_04_2026.md §2 documents the directory split rationale.
//
// ─── Migration map (DOC-3) ──────────────────────────────────────────
//
// Every Graded-backed wrapper in this umbrella migrates to a
// specific Graded<Modality, Lattice, T> specialization per the
// 25_04_2026.md §2.3 calling convention.  The map below is the
// canonical reference; individual headers document their own
// substrate.  The GradedWrapper concept (algebra/GradedTrait.h)
// asserts uniformity across all entries.
//
//   Wrapper                  Substrate                                            Regime
//   ──────────────────────── ─────────────────────────────────────────────────── ──────
//   Linear<T>                Graded<Absolute,    QttSemiring::At<One>,  T>          1
//   Refined<Pred, T>         Graded<Absolute,    BoolLattice<Pred>,     T>          1
//   SealedRefined<Pred, T>   Graded<Absolute,    BoolLattice<Pred>,     T>          1
//   Tagged<T, Source>        Graded<RelativeMonad, TrustLattice<Source>, T>          1
//   Secret<T>                Graded<Comonad,     ConfLattice::At<Secret>, T>        1
//   NumericalTier<Tol, T>    Graded<Absolute,    ToleranceLattice::At<Tol>, T>      1
//   Consistency<Lvl, T>      Graded<Absolute,    ConsistencyLattice::At<Lvl>, T>    1
//   OpaqueLifetime<Sc, T>    Graded<Absolute,    LifetimeLattice::At<Sc>, T>        1
//   DetSafe<Tier, T>         Graded<Absolute,    DetSafeLattice::At<Tier>, T>       1
//   HotPath<Tier, T>         Graded<Absolute,    HotPathLattice::At<Tier>, T>       1
//   Wait<Strategy, T>        Graded<Absolute,    WaitLattice::At<Strategy>, T>      1
//   MemOrder<Tag, T>         Graded<Absolute,    MemOrderLattice::At<Tag>, T>       1
//   Progress<Class, T>       Graded<Absolute,    ProgressLattice::At<Class>, T>     1
//   AllocClass<Tag, T>       Graded<Absolute,    AllocClassLattice::At<Tag>, T>     1
//   CipherTier<Tier, T>      Graded<Absolute,    CipherTierLattice::At<Tier>, T>    1
//   ResidencyHeat<Tier, T>   Graded<Absolute,    ResidencyHeatLattice::At<Tier>, T> 1
//   Vendor<Backend, T>       Graded<Absolute,    VendorLattice::At<Backend>, T>     1 (partial-order)
//   Crash<Class, T>          Graded<Absolute,    CrashLattice::At<Class>, T>        1
//   Monotonic<T, Cmp>        Graded<Absolute,    MonotoneLattice<T,Cmp>, T>         2
//   AppendOnly<T, Storage>   Graded<Absolute,    SeqPrefixLattice<T>,   Storage<T>>  3
//   Stale<T>                 Graded<Absolute,    StalenessSemiring,     T>          4
//   TimeOrdered<T, N, Tag>   Graded<Absolute,    HappensBeforeLattice<N,Tag>, T>    4
//
// (SharedPermission<Tag> is regime-5 and lives in permissions/, not
//  this umbrella; its substrate is Graded<Absolute, FractionalLattice,
//  Tag> as a façade pointing at SharedPermissionPool's atomic state.)
//
// See algebra/GradedTrait.h for the regime-N taxonomy.
//
// ─── Deliberately not graded (DOC-MIGRATE-POLICY) ──────────────────
//
// Nine wrappers in this umbrella are intentionally NOT Graded.  They
// follow non-graded disciplines (RAII, typestate, structural
// constraint) that don't fit the Graded<M, L, T> shape.  A future
// MIGRATE attempt should NOT try to convert them; the policy is
// documented per-wrapper below.
//
//   Wrapper             Discipline                Why NOT graded
//   ─────────────────── ───────────────────────── ──────────────────────────────
//   Machine<S>          typestate machine         States form a transition
//                                                 graph, not a lattice; modeling
//                                                 them as a grade would lose
//                                                 the per-state typing surface.
//   ScopedView<C, T>    lifetime-bounded borrow   Lifetime is a phantom epoch
//                                                 tag, not an ordered grade;
//                                                 ScopedView's discipline is
//                                                 "valid within scope", not
//                                                 "carries a grade value".
//   OwnedRegion<T, Tag> arena-backed ownership    Ownership is structural
//                                                 (region identity), not a
//                                                 grade; combining with Graded
//                                                 is the bridges/ tier's job.
//   Pinned<T>           address stability         Boolean property, not
//                                                 lattice-shaped; cheaper as
//                                                 a CRTP marker.
//   Checked.h           overflow-detecting math   Pure functional primitives
//                                                 (checked_add etc.); no
//                                                 per-value invariant to grade.
//   ConstantTime.h      branch-free crypto        Side-channel-resistant
//                                                 operations; the discipline
//                                                 is timing-shape, not value-
//                                                 shape.
//   NotInherited<T>     structural non-extension  Inheritance constraint, not
//                                                 value invariant; FinalBy
//                                                 idiom belongs in the type
//                                                 system not the algebra.
//   Simd.h              SIMD primitives           Vector arithmetic; the
//                                                 "value" is plural by design,
//                                                 not graded.
//   Workload.h          concurrency policy hint   Scheduler advice, not value
//                                                 invariant; consumed by
//                                                 AdaptiveScheduler.
//
// Five Mutation.h DERIVATIVE wrappers that compose with or layer on
// the already-migrated Monotonic/AppendOnly are also deliberately
// NOT standalone-graded:
//
//   Wrapper                Why NOT separately migrated
//   ─────────────────────  ──────────────────────────────────────────────
//   WriteOnce<T>           State-machine pattern (Unset → Set), not a
//                          graded value.  The discipline is "transition
//                          allowed once in one direction"; that's
//                          Machine<S>'s domain, not Graded&lt;M, L, T&gt;'s.
//                          Migration would lose the typestate clarity.
//   WriteOnceNonNull<T*>   Pointer-sentinel specialization of WriteOnce;
//                          inherits the same state-machine rationale.
//                          Migrating only this would split the design.
//   BoundedMonotonic<T,M>  Composes Monotonic ∩ predicate(≤Max).  The
//                          bound IS a concrete value, not a lattice
//                          element — adding a fresh graded substrate
//                          would duplicate Monotonic's storage with
//                          a redundant grade.  Use the migrated
//                          Monotonic + a separate Refined&lt;bounded_above&gt;
//                          if needed.
//   OrderedAppendOnly<T,K> Composes AppendOnly + per-element key
//                          ordering.  AppendOnly's substrate (regime-3
//                          derived grade) already covers "container
//                          state graded by length"; OrderedAppendOnly
//                          adds an orthogonal per-key invariant best
//                          modeled as a per-element predicate, not a
//                          new lattice.
//   AtomicMonotonic<T,Cmp> Pinned + atomic state.  Same regime-5
//                          situation as SharedPermission (proof-of-
//                          monotonicity / runtime atomic carrier),
//                          but the "proof" here is implicit in the
//                          atomic's value rather than in a separate
//                          phantom token.  Façade migration would add
//                          the diagnostic surface but no new
//                          correctness; deferred until a downstream
//                          consumer needs Graded introspection.
//
// If a future use case demands grading one of these (e.g. a Pinned-
// with-grade), build it as a NEW wrapper layered on top — don't
// retrofit the existing one.  The non-graded wrappers' callers
// depend on the bare shape.

#include <crucible/safety/AllocClass.h>
#include <crucible/safety/Checked.h>
#include <crucible/safety/CipherTier.h>
#include <crucible/safety/ConstantTime.h>
#include <crucible/safety/Consistency.h>
#include <crucible/safety/Crash.h>
#include <crucible/safety/DetSafe.h>
#include <crucible/safety/HotPath.h>
#include <crucible/safety/MemOrder.h>
#include <crucible/safety/Progress.h>
#include <crucible/safety/ResidencyHeat.h>
#include <crucible/safety/Vendor.h>
#include <crucible/safety/Wait.h>
#include <crucible/safety/Diagnostic.h>
#include <crucible/safety/Linear.h>
#include <crucible/safety/Machine.h>
#include <crucible/safety/Mutation.h>
#include <crucible/safety/NotInherited.h>
#include <crucible/safety/NumericalTier.h>
#include <crucible/safety/OpaqueLifetime.h>
#include <crucible/safety/OwnedRegion.h>
#include <crucible/safety/Pinned.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/ScopedView.h>
#include <crucible/safety/SealedRefined.h>
#include <crucible/safety/Secret.h>
#include <crucible/safety/Simd.h>
#include <crucible/safety/Stale.h>
#include <crucible/safety/Tagged.h>
#include <crucible/safety/TimeOrdered.h>
#include <crucible/safety/Workload.h>
