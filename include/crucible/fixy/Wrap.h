#pragma once

// ── crucible::fixy::wrap — Value wrappers under fixy:: ─────────────
//
// Re-export.  Surfaces every remaining value-level safety
// wrapper under `fixy::wrap::` so callers who include only the fixy
// umbrella never have to descend into the safety/ tree to wrap a
// value.  Companion to:
//
//   - fixy/Safety.h — Linear / Secret / ScopedView token mints
//   - fixy/Perm.h   — Permission / SharedPermission token mints
//   - fixy/Mach.h   — Machine token mint
//
// This header covers the FULL CLAUDE.md §XVI catalog:
//
//   • 16 Tier-S canonical outer→inner Graded wrappers
//     (HotPath, DetSafe, NumericalTier, Vendor, ResidencyHeat,
//      CipherTier, AllocClass, Wait, MemOrder, Progress,
//      Stale, Tagged, Refined, Secret, Linear, Computation)
//   • 11 off-tree Graded wrappers
//     (SealedRefined, TimeOrdered, Monotonic, AppendOnly,
//      Consistency, OpaqueLifetime, Crash, Budgeted,
//      EpochVersioned, NumaPlacement, RecipeSpec)
//   • 5 Mutation.h derivative wrappers
//     (WriteOnce, WriteOnceNonNull, BoundedMonotonic,
//      OrderedAppendOnly, AtomicMonotonic)
//   • 8 structural (deliberately not Graded) wrappers
//     (Pinned, NonMovable, ScopedView, OwnedRegion,
//      FixedArray, NotInherited, FinalBy, ct::* primitives)
//
// Three of the canonical wrappers (Linear, Secret, SharedPermission)
// also ship via Safety.h / Perm.h; they are re-exported here too so
// `fixy::wrap::` is the single one-stop directory for value-wrapping.
//
// ── Dual-export discipline (fixy-A4-011) ──────────────────────────
//
// The Linear / Secret / SharedPermission re-exports below also
// appear in fixy::safety:: (Safety.h) and fixy::perm:: (Perm.h)
// respectively — by design.  Both paths name the SAME substrate
// symbol; type identity is drift-checked at compile time by
// `test/test_fixy_umbrella.cpp` (search "fixy-A4-011").  A user TU
// that does `using namespace fixy::safety; using namespace fixy::wrap;`
// works today only because the two using-declarations point at the
// same symbol — divergence would surface as an ADL lookup error.
//
// ── Substrate consumed ─────────────────────────────────────────────
//
// Graded-backed (11 wrappers):
//   safety::Linear<T>                       — move-only linear carrier
//   safety::Refined<Pred, T>                — predicate-checked carrier
//   safety::SealedRefined<Pred, T>          — Refined with no into()
//   safety::Tagged<T, Source>               — provenance / trust tag
//   safety::Secret<T>                       — classified-by-default
//   safety::Monotonic<T, Cmp>               — only-advance value
//   safety::AppendOnly<T, Storage>          — grow-only container
//   safety::Stale<T>                        — value + staleness τ
//   safety::TimeOrdered<T, N, Tag>          — value + vector clock
//   safety::SharedPermission<Tag>           — fractional permission
//   (Linear / Secret / SharedPermission already shipped via Safety/Perm)
//
// Mutation.h derivative wrappers (5):
//   safety::WriteOnce<T>                    — settable exactly once
//   safety::WriteOnceNonNull<T*>            — pointer-slot, no opt tag
//   safety::BoundedMonotonic<T, Max, Cmp>   — Monotonic + upper bound
//   safety::OrderedAppendOnly<T, ...>       — AppendOnly + key order
//   safety::AtomicMonotonic<T, Cmp>         — thread-safe Monotonic
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe   — substrate forwards args; alias preserves NSDMI
//                discipline (every wrapper default-constructs to a
//                well-defined state).
//   TypeSafe   — using-declarations preserve concept gates
//                (PredicateInvocableOn, std::is_pointer_v, etc.).
//   NullSafe   — WriteOnceNonNull's nullptr-sentinel discipline is
//                load-bearing; using-declaration preserves it.
//   MemSafe    — Linear is move-only; alias preserves =delete.
//   BorrowSafe — Stale / TimeOrdered carry happens-before / staleness
//                in the type; alias preserves.
//   ThreadSafe — AtomicMonotonic is Pinned<>; SharedPermission is
//                refcounted; aliases preserve both.
//   LeakSafe   — every wrapper is value-typed; no leak path.
//   DetSafe    — pure value wraps; bit-exact across re-export.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  using-declarations are pure name-lookup directives —
// `sizeof(fixy::wrap::X<T>) == sizeof(safety::X<T>)` for every X.
// No runtime indirection, no extra branch, no extra storage.
//
// ── Universal Mint Pattern surface ──────────────────────────────────
//
// Only Linear / Secret / SharedPermission expose a `mint_*` factory
// today (their `requires` clause encodes the load-bearing token-mint
// gate).  The other wrappers construct directly via their `explicit`
// ctor; the `requires` lives on the wrapped predicate / lattice /
// modality and is enforced at construction site without a free
// helper.  When a wrapper grows a mint factory, add a using-declaration
// here.

#include <crucible/effects/Computation.h>      // canonical: Computation<R,T>
#include <crucible/permissions/Permission.h>  // SharedPermission
#include <crucible/safety/Affine.h>            // FIXY-V-057: substructural at-most-once
#include <crucible/safety/AllocClass.h>        // canonical Tier-S
#include <crucible/safety/Bits.h>              // structural (typed bit-field)
#include <crucible/safety/Borrowed.h>          // non-owning lifetime-tagged view
#include <crucible/safety/IsBorrowedRef.h>     // IsBorrowedRef concept gate
#include <crucible/safety/IsSwmrHandle.h>      // FIXY-V-035: IsSwmrReader (handle shape)
#include <crucible/safety/Budgeted.h>          // off-tree (Space axis)
#include <crucible/safety/Saturated.h>         // {value, was_clamped} carrier
#include <crucible/safety/CipherTier.h>        // canonical Tier-S
#include <crucible/safety/Consistency.h>       // off-tree (Version axis)
#include <crucible/safety/ConstantTime.h>      // structural (ct::* primitives)
#include <crucible/safety/Crash.h>             // off-tree (Effect axis)
#include <crucible/safety/Cyclic.h>            // structural (modular ring-cursor newtype)
#include <crucible/safety/CyclicBuffer.h>      // structural (bounded MRU ring composition)
#include <crucible/safety/DetSafe.h>           // canonical Tier-S
#include <crucible/safety/EpochVersioned.h>    // off-tree (Version axis)
#include <crucible/safety/FixedArray.h>        // structural (bounded stack-array newtype)
#include <crucible/safety/HotPath.h>           // canonical Tier-S (outermost)
#include <crucible/safety/Linear.h>
#include <crucible/safety/MemOrder.h>          // canonical Tier-S
#include <crucible/safety/Mutation.h>          // AppendOnly / Monotonic /
                                               // WriteOnce / WriteOnceNonNull /
                                               // BoundedMonotonic /
                                               // OrderedAppendOnly /
                                               // AtomicMonotonic
#include <crucible/safety/NotInherited.h>      // structural (NotInherited, FinalBy)
#include <crucible/safety/NumaPlacement.h>     // off-tree (Representation)
#include <crucible/safety/NumericalTier.h>     // canonical Tier-S
#include <crucible/safety/OpaqueLifetime.h>    // off-tree (Lifetime axis)
#include <crucible/safety/OwnedRegion.h>       // structural (arena-backed region)
#include <crucible/safety/Pinned.h>            // structural (Pinned + NonMovable)
#include <crucible/safety/Progress.h>          // canonical Tier-S
#include <crucible/safety/RecipeSpec.h>        // off-tree (Precision axis)
#include <crucible/safety/Refined.h>
#include <crucible/safety/RefinedAlgebra.h>    // FIXY-U-096s: all_of predicate combinator
#include <crucible/fixy/wrap/Refined.h>        // granular Refined-family surface (light consumers)
#include <crucible/safety/ResidencyHeat.h>     // canonical Tier-S
#include <crucible/safety/ScopedView.h>        // structural (lifetime borrow)
#include <crucible/safety/SealedRefined.h>
#include <crucible/safety/Secret.h>
#include <crucible/safety/Stale.h>
#include <crucible/safety/SwissTableBuffer.h>  // structural (open-addressing slot buffer)
#include <crucible/safety/SwmrReader.h>        // FIXY-V-035: SwmrReader<auto FnPtr> (function-ptr shape)
#include <crucible/safety/SwmrWriter.h>        // FIXY-V-036: SwmrWriter<auto FnPtr> (function-ptr shape)
#include <crucible/safety/SignatureTraits.h>   // FIXY-V-178: signature_traits<auto FnPtr> family
#include <crucible/safety/GradedExtract.h>     // FIXY-V-178: universal GradedWrapper extractors
#include <crucible/safety/BarrierGuarded.h>   // canonical Tier-S (memory-barrier band)
#include <crucible/safety/Hw.h>               // canonical Tier-S (hardware-instruction band)
#include <crucible/safety/JoinPolicy.h>      // canonical Tier-S (thread-join band)
#include <crucible/safety/SuspendBehavior.h>  // canonical Tier-S (suspend-behavior band)
#include <crucible/safety/SimdWidthPinned.h>  // canonical Tier-S (SIMD-ISA band)
#include <crucible/safety/Tagged.h>
#include <crucible/safety/Path.h>             // FIXY-V-031: Path<Source> + sanitize_path
#include <crucible/safety/TimeOrdered.h>
#include <crucible/safety/Vendor.h>            // canonical Tier-S
#include <crucible/safety/Wait.h>              // canonical Tier-S
#include <crucible/safety/WeakRef.h>           // structural (nullable non-owning ref)
#include <crucible/safety/Witness.h>           // off-tree (Observability axis)

#include <cstdint>       // FIXY-U-020 sentinel uses std::uint64_t
#include <type_traits>   // FIXY-U-020 sentinel uses std::is_same_v

namespace crucible::fixy::wrap {

// ─── Graded-backed wrappers (11) ─────────────────────────────────

// Linear<T> — move-only consume-once.
using ::crucible::safety::Linear;
using ::crucible::safety::mint_linear;
using ::crucible::safety::drop;

// Affine<T> — substructural at-most-once carrier (FIXY-V-057).  Move-only
// like Linear, but silent-drop is PERMITTED (no contract violation on
// destruction without consume).  Substrate is
// Graded<Absolute, QttSemiring::At<Zero>, T> — the QTT grade-0 (erased)
// slot, with the C++ move-only discipline supplying the "max 1" upper
// bound.  Rejects Affine<Permission<Tag>> / Affine<SharedPermission<Tag>>
// via is_already_consume_disciplined — Permission carries an EXACTLY-once
// CSL frame-rule obligation that Affine would downgrade to optional.
// §XXI mint_affine factory; drop(Affine<T>&&) is the explicit-discard
// affine-no-op (peer to safety::drop for Linear).
using ::crucible::safety::Affine;
using ::crucible::safety::mint_affine;
using ::crucible::safety::is_already_consume_disciplined;
using ::crucible::safety::is_already_consume_disciplined_v;

// Refined<Pred, T> family + predicates + combinator + implication trait
// are surfaced via the granular <crucible/fixy/wrap/Refined.h> header
// (included above) — single source of truth.  Light foundational
// consumers (Types.h) include that header directly to avoid this
// umbrella's OwnedRegion -> Arena transitive pull (fixy-A2-014 guard).

// SealedRefined<Pred, T> — Refined with no into() extractor.
using ::crucible::safety::SealedRefined;
using ::crucible::safety::mint_sealed_refined;  // FIXY-U-115 §XXI surface

// Tagged<T, Source> — phantom-tag provenance / trust marker.
using ::crucible::safety::Tagged;
using ::crucible::safety::mint_tagged;          // FIXY-U-115 §XXI surface

// FIXY-V-031: Path<Source> + sanitize_path — typed filesystem path
// with explicit trust-boundary promotion (External → Sanitized via
// PathTraversal-policy sanitizer).  Substrate for FIXY-V-127/V-232/V-233.
template <typename Source>
using Path = ::crucible::safety::Path<Source>;
using ::crucible::safety::PathTraversal;
using ::crucible::safety::PathTraversalError;
using ::crucible::safety::sanitize_path;
using ::crucible::safety::MAX_PATH_BYTES;

// Secret<T> — classified-by-default carrier.
using ::crucible::safety::Secret;
using ::crucible::safety::mint_secret;

// Monotonic<T, Cmp> — only-advance per Cmp.
using ::crucible::safety::Monotonic;

// AppendOnly<T, Storage> — grow-only container.
using ::crucible::safety::AppendOnly;

// Stale<T> — value paired with staleness semiring element τ.
using ::crucible::safety::Stale;

// TimeOrdered<T, N, Tag> — value paired with N-process vector clock.
using ::crucible::safety::TimeOrdered;

// Borrowed<T, ParentTag> / BorrowedRef<T, ParentTag> — non-owning
// lifetime-tagged carriers for span-like views into parent structures.
// safety/Borrowed.h primary symbols — surfaced here per the FIXY-U-093
// migration's MerkleDag.h trace_entry getter signatures.
using ::crucible::safety::Borrowed;
using ::crucible::safety::BorrowedRef;

// WeakRef<T> — nullable, non-owning, may-be-evicted reference (the
// cache-slot / back-pointer primitive).  Complements BorrowedRef (which
// is must-be-present) by filling the may-be-null quadrant BorrowedRef's
// own doc delegates away.  Surfaced through fixy::wrap:: so the cache
// consumers (WRAP-RegionCache-1 #986 regions_[8], WRAP-RE-1 #993
// ReplayEngine parent back-pointer) reach the wrapper through the
// umbrella.  Deliberately-not-graded structural wrapper, peer to
// BorrowedRef.
using ::crucible::safety::WeakRef;

// Saturated<T> — saturating-arithmetic result carrying {value,
// was_clamped} discriminant.  safety/Saturated.h primary symbol —
// surfaced here per FIXY-U-093 (MerkleDag.h compute_storage_nbytes).
using ::crucible::safety::Saturated;

// add_sat_checked / sub_sat_checked / mul_sat_checked — saturating
// arithmetic primitives returning Saturated<T>{value, was_clamped}.
// safety/Saturated.h free functions — surfaced here per FIXY-U-096b
// (Saturate.h migration of crucible::sat::*_det / *_from variants).
using ::crucible::safety::add_sat_checked;
using ::crucible::safety::sub_sat_checked;
using ::crucible::safety::mul_sat_checked;

// SharedPermission<Tag> — fractional permission carrier.  The
// substrate lives in permissions/Permission.h but is re-exported
// into crucible::safety via the permissions namespace, so it is
// reachable through ::crucible::safety::SharedPermission per the
// existing fixy/Perm.h convention.
using ::crucible::safety::SharedPermission;
using ::crucible::safety::mint_permission_share;

// ─── Mutation.h derivative wrappers (5) ──────────────────────────

// WriteOnce<T> — set exactly once, then read-only.
using ::crucible::safety::WriteOnce;

// WriteOnceNonNull<Ptr> — pointer-slot one-set; nullptr-sentinel.
using ::crucible::safety::WriteOnceNonNull;

// BoundedMonotonic<T, Max, Cmp> — Monotonic + upper bound.
using ::crucible::safety::BoundedMonotonic;

// OrderedAppendOnly<T, KeyFn, Cmp, Storage> — AppendOnly + key order.
using ::crucible::safety::OrderedAppendOnly;

// AtomicMonotonic<T, Cmp> — thread-safe Monotonic over std::atomic<T>.
using ::crucible::safety::AtomicMonotonic;

// ─── Canonical Tier-S outer→inner (11 missing of 16) ─────────────
// Order follows CLAUDE.md §XVI canonical wrapper-nesting (outer→inner):
//   HotPath ⊃ DetSafe ⊃ NumericalTier ⊃ Vendor ⊃ ResidencyHeat ⊃
//   CipherTier ⊃ AllocClass ⊃ Wait ⊃ MemOrder ⊃ Progress ⊃
//   Stale ⊃ Tagged ⊃ Refined ⊃ Secret ⊃ Linear ⊃ Computation

// Per-tier convenience aliases (e.g. `HotH`/`PureD`/`SpinW`) live in
// substrate `detail::*_layout::` namespaces and are intentionally NOT
// public.  Consumers pick a tier inline: `HotPath<HotPathTier_v::Hot, T>`,
// `Wait<WaitStrategy_v::SpinPause, T>`, etc.  Public per-tier aliases
// (with friendlier spellings like `Hot`/`SpinPause`) live in the
// per-wrapper sub-namespaces under safety::hot_path::, safety::wait::,
// etc.; access through that path if a tier-shorthand is desired.

// HotPath<HotPathTier, T> — locality-class declaration (outermost).
using ::crucible::safety::HotPath;
using ::crucible::safety::HotPathLattice;
using ::crucible::safety::HotPathTier_v;

// DetSafe<DetSafeTier, T> — replay-determinism class.
using ::crucible::safety::DetSafe;
using ::crucible::safety::DetSafeLattice;
using ::crucible::safety::DetSafeTier_v;

// NumericalTier<Tolerance, T> — recipe-determinism tier.
using ::crucible::safety::NumericalTier;
using ::crucible::safety::Tolerance;
using ::crucible::safety::ToleranceLattice;

// Vendor<VendorBackend, T> — Mimic per-vendor lowering target.
using ::crucible::safety::Vendor;
using ::crucible::safety::VendorLattice;
using ::crucible::safety::VendorBackend_v;

// ResidencyHeat<ResidencyHeatTag, T> — memory-tier residency.
using ::crucible::safety::ResidencyHeat;
using ::crucible::safety::ResidencyHeatLattice;
using ::crucible::safety::ResidencyHeatTag_v;

// CipherTier<CipherTierTag, T> — Cipher hot/warm/cold tier.
using ::crucible::safety::CipherTier;
using ::crucible::safety::CipherTierLattice;
using ::crucible::safety::CipherTierTag_v;

// AllocClass<AllocClassTag, T> — allocation origin (Stack/Arena/Heap/...).
using ::crucible::safety::AllocClass;
using ::crucible::safety::AllocClassLattice;
using ::crucible::safety::AllocClassTag_v;

// Wait<WaitStrategy, T> — spin/park/block strategy for atomic waits.
using ::crucible::safety::Wait;
using ::crucible::safety::WaitLattice;
using ::crucible::safety::WaitStrategy_v;

// MemOrder<MemOrderTag, T> — memory-order discipline (Relaxed/AcqRel/SeqCst).
using ::crucible::safety::MemOrder;
using ::crucible::safety::MemOrderLattice;
using ::crucible::safety::MemOrderTag_v;

// Progress<ProgressClass, T> — Bounded/Productive/MayDiverge termination class.
using ::crucible::safety::Progress;
using ::crucible::safety::ProgressLattice;
using ::crucible::safety::ProgressClass_v;

// Computation<R, T> — Met(X) effect-row carrier (innermost canonical).
using ::crucible::effects::Computation;

// ─── Off-tree Graded wrappers (7 missing of 11) ───────────────────

// Consistency<Consistency_v, T> — version-axis consistency level.
// Per-tier aliases under safety::consistency::Strong/BoundedStaleness/Eventual.
using ::crucible::safety::Consistency;
using ::crucible::safety::ConsistencyLattice;
using ::crucible::safety::Consistency_v;

// OpaqueLifetime<Lifetime_v, T> — lifetime-axis scoping.  Per-tier
// aliases live under safety::opaque_lifetime:: (PerFleet/PerProgram/
// PerRequest); use them via that path or pick a tier inline.
using ::crucible::safety::OpaqueLifetime;
using ::crucible::safety::LifetimeLattice;
using ::crucible::safety::Lifetime_v;

// Crash<CrashClass, T> — effect-axis crash discipline.  Per-tier
// aliases live under safety::crash:: (NoThrow/ErrorReturn/Throw/Abort)
// rather than at top-level; expose the class + lattice + enum here
// and let consumers pick the tier inline as `Crash<NoThrow, T>`.
using ::crucible::safety::Crash;
using ::crucible::safety::CrashLattice;
using ::crucible::safety::CrashClass_v;

// Budgeted<T> — space-axis bit/byte budget (BitsBudget + PeakBytes).
using ::crucible::safety::Budgeted;
using ::crucible::safety::BitsBudget;
using ::crucible::safety::BitsBudgetLattice;
using ::crucible::safety::PeakBytes;
using ::crucible::safety::PeakBytesLattice;

// EpochVersioned<T> — version-axis epoch + generation pair.
using ::crucible::safety::EpochVersioned;
using ::crucible::safety::Epoch;
using ::crucible::safety::EpochLattice;
using ::crucible::safety::Generation;
using ::crucible::safety::GenerationLattice;

// NumaPlacement<T> — representation-axis NUMA affinity placement.
using ::crucible::safety::NumaPlacement;
using ::crucible::safety::AffinityLattice;
using ::crucible::safety::AffinityMask;
using ::crucible::safety::NumaNodeId;
using ::crucible::safety::NumaNodeLattice;

// RecipeSpec<T> — precision-axis recipe-family pinning.
using ::crucible::safety::RecipeSpec;
using ::crucible::safety::RecipeFamily;
using ::crucible::safety::RecipeFamilyLattice;

// Witness<Witness_v, T> — observability-axis proof-strength carrier
// (FIXY-V-054).  Comonad modality, 4-tier chain
// UNWITNESSED ⊑ TYPE_CHECKED ⊑ TEST_PASSED ⊑ FORMALLY_VERIFIED.
// Companion §XXI mint factory `mint_witness<Tier, T>(args...)` and
// per-tier convenience aliases below.  Federation cache key carved
// out via the WRAPPER_WITNESS_TAG row_hash salt (FIXY-V-055).
using ::crucible::safety::Witness;
using ::crucible::safety::WitnessLattice;
using ::crucible::safety::Witness_v;
using ::crucible::safety::mint_witness;

// Per-tier convenience aliases — mirror safety::witness_tier::
// (FIXY-V-054 surface).  Lets consumers write the four canonical
// proof-strength variants without spelling out the tier enum.
template <typename T>
using Unwitnessed = ::crucible::safety::Witness<
    ::crucible::safety::Witness_v::UNWITNESSED, T>;
template <typename T>
using TypeChecked = ::crucible::safety::Witness<
    ::crucible::safety::Witness_v::TYPE_CHECKED, T>;
template <typename T>
using TestPassed = ::crucible::safety::Witness<
    ::crucible::safety::Witness_v::TEST_PASSED, T>;
template <typename T>
using FormallyVerified = ::crucible::safety::Witness<
    ::crucible::safety::Witness_v::FORMALLY_VERIFIED, T>;

// ─── Tier-S hardware / concurrency band Graded wrappers ───────────
// Newer than the §XVI off-tree set above (V-254 era).  Hw<Tier, T>
// pins an instruction-capability band: Absolute modality, 5-tier
// chain NoneAllowed ⊑ Scalar ⊑ Vectorizable ⊑ NonDeterministicTsc ⊑
// PrivilegedMsr.  Companion §XXI factory `mint_hw<Tier, T>(args...)`;
// per-tier aliases live under safety::hw_pin:: — pick a tier inline
// as `Hw<HwInstruction_v::Vectorizable, T>`.
using ::crucible::safety::Hw;
using ::crucible::safety::HwInstructionLattice;
using ::crucible::safety::HwInstruction_v;
using ::crucible::safety::mint_hw;

// BarrierGuarded<Tier, T> — memory-barrier-strength band.  Absolute
// modality over BarrierStrengthLattice; companion §XXI factory
// `mint_barrier_guarded<Tier, T>(args...)`.  Per-tier aliases live
// under safety:: — pick a tier inline as
// `BarrierGuarded<BarrierStrength_v::SeqCst, T>`.
using ::crucible::safety::BarrierGuarded;
using ::crucible::safety::BarrierStrengthLattice;
using ::crucible::safety::BarrierStrength_v;
using ::crucible::safety::mint_barrier_guarded;

// SimdWidthPinned<Isa, T> — SIMD-ISA-width band.  Absolute modality
// over SimdIsaLattice; companion §XXI factory
// `mint_simd_width_pinned<Isa, T>(args...)`.  Pick an ISA inline as
// `SimdWidthPinned<SimdIsa_v::Avx512Bw, T>`.
using ::crucible::safety::SimdWidthPinned;
using ::crucible::safety::SimdIsaLattice;
using ::crucible::safety::SimdIsa_v;
using ::crucible::safety::mint_simd_width_pinned;

// JoinPolicy<Tier, T> — thread-join-discipline band.  Absolute
// modality over JoinPolicyLattice; companion §XXI factory
// `mint_join_policy<Tier, T>(args...)`.  Per-tier aliases live under
// safety:: — pick a tier inline as `JoinPolicy<JoinPolicy_v::JOIN_ALL, T>`.
using ::crucible::safety::JoinPolicy;
using ::crucible::safety::JoinPolicyLattice;
using ::crucible::safety::JoinPolicy_v;
using ::crucible::safety::mint_join_policy;

// SuspendBehavior<Behavior, T> — suspend-resilience band.  Absolute
// modality over SuspendBehaviorLattice (Unknown ⊑ PausesOnSuspend ⊑
// KeepsTicking); companion §XXI factory
// `mint_suspend_behavior<Behavior, T>(args...)`.  Pick a tier inline
// as `SuspendBehavior<SuspendBehavior_v::KeepsTicking, T>`.
using ::crucible::safety::SuspendBehavior;
using ::crucible::safety::SuspendBehaviorLattice;
using ::crucible::safety::SuspendBehavior_v;
using ::crucible::safety::mint_suspend_behavior;

// ─── Structural wrappers (deliberately not Graded) ────────────────
// Per CLAUDE.md §XVI: these follow non-Graded disciplines (RAII,
// typestate, address-stability, structural constraint) that don't fit
// the Graded<M, L, T> shape but ARE value-level wrappers.  Surfaced
// here so consumers don't have to descend into safety/ for them.

// Pinned<T> — address-stability marker (CRTP base).
using ::crucible::safety::Pinned;
// NonMovable<T> — Pinned's stronger sibling: also non-movable.
using ::crucible::safety::NonMovable;

// ScopedView<Carrier, Tag> — lifetime-bounded borrow for inspection.
using ::crucible::safety::ScopedView;
// mint_view<Tag>(carrier) — §XXI token mint for ScopedView<Carrier, Tag>.
// Calls view_ok(carrier, type_identity<Tag>{}) for one runtime state
// assertion; grep-discoverable via `mint_view<`.
using ::crucible::safety::mint_view;
// no_scoped_view_field_check<T>() — Tier-2 reflection audit: static_asserts
// T has no ScopedView<...> field (whose lifetime would outlive the carrier).
using ::crucible::safety::no_scoped_view_field_check;
// IsBorrowedRef<T> — concept witnessing T is a safety::BorrowedRef<U>.
// Used in static_asserts on member-type aliases (e.g. ReplayEngine::PoolBorrow).
using ::crucible::safety::extract::IsBorrowedRef;

// FIXY-V-035 — SWMR (single-writer-multiple-reader) handle shapes.  Two
// independent concept surfaces routed through the fixy::wrap:: umbrella so
// production consumers (KernelCache reader-thread dispatcher per 27_04 §3.6,
// AtomicSnapshot observe-metrics broadcast per QUEUE-7, SwmrSession per
// SAFEINT-B18) reach both shapes without descending into safety/:
//
//   SwmrReader<auto FnPtr>      Function-pointer-shape concept: FnPtr names
//                               a function taking one parameter satisfying
//                               IsSwmrReader on its handle type AND whose
//                               return is NOT an OwnedRegion (loading a
//                               region is a Linear-discipline write, not a
//                               reader-thread query — handled separately by
//                               SwmrWriter, ships as FIXY-V-036).
//   IsSwmrReader<T>             Handle-type-shape concept: T (after stripping
//                               cv/ref) walks-and-quacks like a SWMR-reader
//                               handle (D07 substrate's member-function
//                               signature pattern; companion helpers below).
//
// Both are deliberately NOT graded — the reader shape is a structural
// constraint over the handle's API, not a value the type-system can carry
// in a lattice.  Peer to IsBorrowedRef in this section.
//
// Companion traits surfaced alongside so consumers can extract the
// payload type without re-routing:
//
//   is_swmr_reader_function_v<auto FnPtr>   bool: FnPtr is a SwmrReader
//   swmr_reader_handle_value_t<auto FnPtr>  payload-of-the-handle
//   swmr_reader_returned_value_t<auto FnPtr> payload-of-the-return
//   swmr_reader_value_consistent_v<auto F>  bool: handle and return agree
//   is_swmr_reader_v<T>                     bool: T is a SwmrReader handle
//   swmr_reader_value_t<T>                  payload of T (ill-formed if not)
using ::crucible::safety::extract::SwmrReader;
using ::crucible::safety::extract::is_swmr_reader_function_v;
using ::crucible::safety::extract::swmr_reader_handle_value_t;
using ::crucible::safety::extract::swmr_reader_returned_value_t;
using ::crucible::safety::extract::swmr_reader_value_consistent_v;
using ::crucible::safety::extract::IsSwmrReader;
using ::crucible::safety::extract::is_swmr_reader_v;
using ::crucible::safety::extract::swmr_reader_value_t;

// FIXY-V-036 — SWMR writer-side shapes.  Companion to FIXY-V-035 reader
// surfaces; the substrate carves the SWMR ecosystem into producer (publish)
// and consumer (load) halves so dispatchers can route by shape per 27_04 §3.6.
//
//   SwmrWriter<auto FnPtr>      Function-pointer-shape concept: FnPtr names
//                               a function whose param 0 is a non-const
//                               rvalue-ref to an IsSwmrWriter-satisfying
//                               handle AND param 1 is the value-to-publish
//                               (by const-ref OR by value) AND return is
//                               void (publishes don't yield).  Distinct
//                               from ProducerEndpoint (which moves an
//                               OwnedRegion); SwmrWriter EXCLUDES that.
//   IsSwmrWriter<T>             Handle-type-shape concept: T (after cv/ref
//                               strip) walks-and-quacks like a SWMR-writer
//                               handle (D07 substrate's publish(T const&)
//                               member-function pattern).
//
// Companion traits parallel the reader side, with `published_value_t`
// replacing `returned_value_t` since writes consume value, not produce:
//
//   is_swmr_writer_function_v<auto FnPtr>      bool: FnPtr is a SwmrWriter
//   swmr_writer_handle_value_t<auto FnPtr>     payload-of-the-handle
//   swmr_writer_published_value_t<auto FnPtr>  type of param 1 (the value)
//   swmr_writer_value_consistent_v<auto F>     bool: handle and value agree
//   is_swmr_writer_v<T>                        bool: T is a SwmrWriter handle
//   swmr_writer_value_t<T>                     payload of T (ill-formed if not)
using ::crucible::safety::extract::SwmrWriter;
using ::crucible::safety::extract::is_swmr_writer_function_v;
using ::crucible::safety::extract::swmr_writer_handle_value_t;
using ::crucible::safety::extract::swmr_writer_published_value_t;
using ::crucible::safety::extract::swmr_writer_value_consistent_v;
using ::crucible::safety::extract::IsSwmrWriter;
using ::crucible::safety::extract::is_swmr_writer_v;
using ::crucible::safety::extract::swmr_writer_value_t;

// FIXY-V-178 — function-signature reflection helpers.  The substrate's
// `signature_traits<auto FnPtr>` (safety/SignatureTraits.h, the
// "SignatureTraits" surface) is consumed by InferredRow + Inferred-
// PermissionTags to read a pipeline-stage function's parameter/return
// shapes via P2996 reflection.  Routed through fixy::wrap:: so band-3
// pipeline-stage authors reach the introspection surface without
// descending into safety/.  All are NTTP-templated metafunctions over
// a `auto FnPtr` constant — pure type-level, zero runtime cost.
//
//   signature_traits<auto FnPtr>     full traits struct (arity / params
//                                    / return / function-type / noexcept)
//   param_type_t<auto FnPtr, I>      I-th parameter type
//   return_type_t<auto FnPtr>        return type (void supported)
//   function_type_t<auto FnPtr>      bare function type (not the pointer)
//   arity_v<auto FnPtr>              parameter count
//   is_noexcept_v<auto FnPtr>        noexcept-ness of the function
using ::crucible::safety::extract::signature_traits;
using ::crucible::safety::extract::param_type_t;
using ::crucible::safety::extract::return_type_t;
using ::crucible::safety::extract::function_type_t;
using ::crucible::safety::extract::arity_v;
using ::crucible::safety::extract::is_noexcept_v;

// FIXY-V-178 — universal GradedWrapper extractors (the "GradedExtract"
// surface, safety/GradedExtract.h).  The IsGradedWrapper *concept* is
// already reachable via fixy/Is.h (concepts cannot be re-exported by a
// using-decl — Is.h redefines it); what was MISSING at fixy level were
// the extract METAFUNCTIONS that row_hash_contribution and the grade-
// aware shape recognizers use to drill into a wrapper's substrate.
// Surfacing them here closes the FIXY-U-061 fixy::algebra::dim::
// surface-completeness gap.  All are alias / variable templates
// constrained on IsGradedWrapper — pure type-level, zero runtime cost.
//
//   value_type_of_t<W>              wrapper's user-facing value type
//   lattice_of_t<W>                 wrapper's Lattice / Semiring instance
//   grade_of_t<W>                   lattice's grade element type
//   graded_type_of_t<W>             substrate Graded<M, L, T> instance
//   modality_of_v<W>                wrapper's ModalityKind value
//   is_graded_wrapper_v<W>          variable-template form of the concept
//   is_graded_specialization_v<T>   T is a bare Graded<M, L, T> (not a wrap)
using ::crucible::safety::extract::value_type_of_t;
using ::crucible::safety::extract::lattice_of_t;
using ::crucible::safety::extract::grade_of_t;
using ::crucible::safety::extract::graded_type_of_t;
using ::crucible::safety::extract::modality_of_v;
using ::crucible::safety::extract::is_graded_wrapper_v;
using ::crucible::safety::extract::is_graded_specialization_v;

// OwnedRegion<T, Tag> — arena-backed exclusive region.
using ::crucible::safety::OwnedRegion;

// FixedArray<T, N> — bounded-capacity stack-allocated array newtype.
// Replaces raw `T buf[N]` C arrays where the capacity is fixed at
// compile time and every slot is logically used.  Distinct type
// identity from std::array<T, N> (catches accidental swaps); NSDMI
// zero-init (closes the bare-array uninit-before-store window);
// no exception-throwing accessor (Crucible is -fno-exceptions);
// Refined<bounded_above<N-1>>-typed at() for proof-token access.
// Surfaced through fixy::wrap:: so production sites (FIXY-U-096a
// StorageNbytes.h, future Lower.h FixedArray-1 migration) reach
// the wrapper through the umbrella instead of descending into
// crucible/safety/FixedArray.h.
using ::crucible::safety::FixedArray;

// Cyclic<T, N> — free-running modular-counter newtype for power-of-two
// ring cursors.  Carries the "this counter is a ring slot, read me
// mod N" invariant that a bare `uint32_t head_` discards: index() is
// the masked next-write slot, index_back(i) the i-th most-recent slot,
// advance() the wrapping increment.  Surfaced through fixy::wrap:: so
// the ring consumers (WRAP-RegionCache-4 #989 RegionCache::head_,
// WRAP-Transaction-4 #1063 TransactionLog::head_) reach the wrapper
// through the umbrella instead of descending into
// crucible/safety/Cyclic.h.  Deliberately-not-graded structural
// wrapper (modular arithmetic has no useful lattice), peer to
// FixedArray / Saturated.
using ::crucible::safety::Cyclic;

// CyclicBuffer<T, N> — bounded MRU ring buffer composing FixedArray<T,N>
// (slots) + Cyclic<size_t,N> (write cursor) + BoundedMonotonic<size_t,N>
// (saturating fill).  Promotes the recurring "remember the last N
// events" triple to one audited type: claim() yields the next-write slot
// for in-place mutation, recent(i) is MRU-first reverse scan, size()
// saturates at N.  Surfaced through fixy::wrap:: so the ring consumers
// (WRAP-Transaction-4 #1063 TransactionLog ring, WRAP-RegionCache-4 #989
// RegionCache slot ring) reach the composition through the umbrella.
// Deliberately-not-graded structural wrapper, peer to Cyclic.
using ::crucible::safety::CyclicBuffer;

// NotInherited<T> (concept) / FinalBy<T> (CRTP base) — structural
// non-extensibility.  NotInherited is a `concept`, not a class, so it
// is brought in as a name only (no per-tier alias possible).
using ::crucible::safety::NotInherited;
using ::crucible::safety::assert_not_inherited;
using ::crucible::safety::FinalBy;

// Bits<E, Invariants...> — typed bit-field over a scoped enum E.  Distinct
// type identity from raw `uint8_t` / `uint32_t` (catches accidental swaps);
// const-correct ops (operator| / & / ~); contract-checked invariant traits.
// Surfaced through fixy::wrap:: so production sites (FIXY-U-096o Graph.h
// NodeFlags) reach the wrapper through the umbrella instead of descending
// into crucible/safety/Bits.h.
using ::crucible::safety::Bits;

// SwissTableBuffer<Slot> — structural RAII open-addressing slot buffer.
// Replaces raw `void*` + manual aligned_alloc/free dance in interned
// pools (ExprPool, RecipePool) where the Swiss-table backing IS the
// canonical owner of the aligned coupled allocation.  Move-only with
// nothrow move (`is_nothrow_move_constructible`).  Surfaced through
// fixy::wrap:: so production sites (FIXY-U-096n ExprPool.h) reach
// the wrapper through the umbrella instead of descending into
// crucible/safety/SwissTableBuffer.h.
using ::crucible::safety::SwissTableBuffer;

// ─── ConstantTime primitives (ct:: sub-namespace) ─────────────────
// Branch-free primitives for crypto paths and Cipher key handling.
// CLAUDE.md §XVI structural-wrappers entry: ConstantTime<T> — Crucible's
// ct:: lives at ::crucible::safety::ct (free functions, not a class).
namespace ct {
using ::crucible::safety::ct::mask_from_bit;
using ::crucible::safety::ct::select;
using ::crucible::safety::ct::eq;
using ::crucible::safety::ct::less;
using ::crucible::safety::ct::is_zero;
using ::crucible::safety::ct::cswap;
}  // namespace ct

// ─── Per-tier convenience-alias sub-namespaces — FIXY-U-093 follow-up ──
//
// Every public Graded-backed wrapper in safety/ ships a peer
// sub-namespace (e.g., `safety::residency_heat::{Hot,Warm,Cold}`,
// `safety::wait::{SpinPause,BoundedSpin,...}`) holding the per-tier
// convenience aliases.  These are public substrate surface but were
// originally omitted from fixy::wrap:: under the (incorrect) assumption
// that they lived in `detail::*_layout::` namespaces.  Audit during the
// MerkleDag.h migration revealed they are public top-level namespaces;
// re-export them here so consumers can write
// `fixy::wrap::residency_heat::Hot<CompiledKernel*>` instead of
// the verbose inline form.  13 sub-namespaces — one per Tier-S canonical
// wrapper (10) plus the three off-tree Graded wrappers that ship public
// sub-namespaces (Consistency / OpaqueLifetime / Crash).

namespace hot_path        = ::crucible::safety::hot_path;
namespace det_safe        = ::crucible::safety::det_safe;
namespace numerical_tier  = ::crucible::safety::numerical_tier;
namespace vendor          = ::crucible::safety::vendor;
namespace residency_heat  = ::crucible::safety::residency_heat;
namespace cipher_tier     = ::crucible::safety::cipher_tier;
namespace alloc_class     = ::crucible::safety::alloc_class;
namespace wait            = ::crucible::safety::wait;
namespace mem_order       = ::crucible::safety::mem_order;
namespace progress        = ::crucible::safety::progress;
namespace consistency     = ::crucible::safety::consistency;
namespace opaque_lifetime = ::crucible::safety::opaque_lifetime;
namespace crash           = ::crucible::safety::crash;

// ── FIXY-V-224 — fixy/Fs.h umbrella re-export ───────────────────────
//
// Lift the V-224 fixy/Fs.h substrate (open_mode / flag / sync_op /
// atomicity type-tag namespaces + grant::fs:: + Dirfd + mint_file +
// sync + commit_atomic + composite mints) up under fixy::wrap::fs::
// so callers who include only <crucible/fixy/Wrap.h> reach the whole
// filesystem mint surface through one entry point.

}  // namespace crucible::fixy::wrap

#include <crucible/fixy/Fs.h>          // FIXY-V-224 substrate

namespace crucible::fixy::wrap::fs {

// Type-tag namespaces — empty-final marker tags partitioning the
// open-mode / flag / sync-op / atomicity axes.
namespace open_mode  = ::crucible::fixy::fs::open_mode;
namespace flag       = ::crucible::fixy::fs::flag;
namespace sync_op    = ::crucible::fixy::fs::sync_op;
namespace atomicity  = ::crucible::fixy::fs::atomicity;

// Grant tag tree — what the user writes at every Fs call site.
namespace grant      = ::crucible::fixy::grant::fs;

// Type aliases — Path<Source>, Dirfd, ctx-fit concepts.
template <typename Source>
using Path = ::crucible::fixy::fs::Path<Source>;
using ::crucible::fixy::fs::Dirfd;
using ::crucible::fixy::fs::open_dirfd;
using ::crucible::fixy::fs::CtxAdmitsIoBlock;
using ::crucible::fixy::fs::CtxFitsFileMint;
using ::crucible::fixy::fs::CtxFitsSync;
using ::crucible::fixy::fs::CtxFitsCommitAtomic;

// §XXI mint factories + composition aliases.
using ::crucible::fixy::fs::mint_file;
using ::crucible::fixy::fs::sync;
using ::crucible::fixy::fs::commit_atomic;
using ::crucible::fixy::fs::read_only;
using ::crucible::fixy::fs::mint_durable_truncate_file;
using ::crucible::fixy::fs::mint_durable_append_file;

}  // namespace crucible::fixy::wrap::fs

// ── FIXY-V-225 — fixy/Mmap.h umbrella re-export ──────────────────────
//
// Lift the V-225 mmap surface (prot / share / advice type-tag
// namespaces + grant::mmap:: + OwnedMmap + ctx-fit concepts +
// mint_mmap + mint_mmap_anon + advise + advise_release_aware) under
// fixy::wrap::mmap:: so callers reach the whole region-mapping mint
// surface through one entry point.

#include <crucible/fixy/Mmap.h>         // FIXY-V-225 substrate

namespace crucible::fixy::wrap::mmap {

// Type-tag namespaces — empty-final marker tags partitioning the
// prot / share / advice axes.
namespace prot   = ::crucible::fixy::mmap::prot;
namespace share  = ::crucible::fixy::mmap::share;
namespace advice = ::crucible::fixy::mmap::advice;

// Grant tag tree — what the user writes at every Mmap call site.
namespace grant  = ::crucible::fixy::grant::mmap;

// Type aliases — OwnedMmap + concepts.
template <typename Tag, typename Prot, typename Share>
using OwnedMmap = ::crucible::fixy::mmap::OwnedMmap<Tag, Prot, Share>;

using ::crucible::fixy::mmap::CtxAdmitsIoBlock;
using ::crucible::fixy::mmap::CtxFitsMmapMint;
using ::crucible::fixy::mmap::CtxFitsAnonMmapMint;
using ::crucible::fixy::mmap::CtxFitsSafeAdvise;
using ::crucible::fixy::mmap::CtxFitsReleaseAwareAdvise;

// §XXI mint factories + effect operations.
using ::crucible::fixy::mmap::mint_mmap;
using ::crucible::fixy::mmap::mint_mmap_anon;
using ::crucible::fixy::mmap::advise;
using ::crucible::fixy::mmap::advise_release_aware;

}  // namespace crucible::fixy::wrap::mmap

// ── FIXY-V-226 — fixy/Io.h umbrella re-export ────────────────────────
//
// Lift the V-226 async-I/O surface (engine / zerocopy / ring_flag type-
// tag namespaces + grant::io:: + IoUringRing Linear RAII handle + ctx-
// fit concepts + mint_io_uring_ring + mint_zerocopy_transfer) under
// fixy::wrap::io:: so callers reach the whole async-engine surface
// through one entry point.

#include <crucible/fixy/Io.h>           // FIXY-V-226 substrate

namespace crucible::fixy::wrap::io {

// Type-tag namespaces — empty-final marker tags partitioning the
// engine / zerocopy / ring_flag axes.
namespace engine    = ::crucible::fixy::io::engine;
namespace zerocopy  = ::crucible::fixy::io::zerocopy;
namespace ring_flag = ::crucible::fixy::io::ring_flag;

// Grant tag tree — what the user writes at every Io call site.
namespace grant     = ::crucible::fixy::grant::io;

// Type aliases — IoUringRing + concepts.
using IoUringRing = ::crucible::fixy::io::IoUringRing;

using ::crucible::fixy::io::CtxAdmitsIoBlock;
using ::crucible::fixy::io::CtxFitsIoUringMint;
using ::crucible::fixy::io::CtxFitsZerocopyMint;

// §XXI mint factories.
using ::crucible::fixy::io::mint_io_uring_ring;
using ::crucible::fixy::io::mint_zerocopy_transfer;

}  // namespace crucible::fixy::wrap::io

// ── FIXY-V-227 — fixy/Cipher.h umbrella re-export ─────────────────────
//
// Three composite stances for the Cipher persistence layer's
// distinct durability + atomicity postures (warm-tier writer /
// cold-tier durable writer / HEAD-pointer advance).  Re-exposed at
// fixy::wrap::cipher:: so callers reach the stance concepts
// (`IsCipherWarmWriterStance` / `IsCipherColdWriterStance` /
// `IsHeadAdvanceStance`) and the canonical stance packs through one
// short path.  V-228's mint factories will fold the stance concepts
// into their `requires` clauses.

#include <crucible/fixy/Cipher.h>           // FIXY-V-227 substrate

namespace crucible::fixy::wrap::cipher {

// Stance-engagement concepts.
using ::crucible::fixy::cipher::IsCipherWarmWriterStance;
using ::crucible::fixy::cipher::IsCipherColdWriterStance;
using ::crucible::fixy::cipher::IsHeadAdvanceStance;

// Boolean-variable-template projection for the concepts (lets
// callers static_assert against the predicate without needing to
// wrap in a requires-clause).
using ::crucible::fixy::cipher::engages_warm_writer_stance_v;
using ::crucible::fixy::cipher::engages_cold_writer_stance_v;
using ::crucible::fixy::cipher::engages_head_advance_stance_v;

// Canonical stance packs (documentation + grep target).
using ::crucible::fixy::cipher::CipherWarmWriterStance;
using ::crucible::fixy::cipher::CipherColdWriterStance;
using ::crucible::fixy::cipher::HeadAdvanceStance;

// Pack-satisfaction adapters (let callers query "does this pack
// satisfy stance X?" without re-typing the parameter-pack expansion).
using ::crucible::fixy::cipher::stance_pack_satisfies_warm_v;
using ::crucible::fixy::cipher::stance_pack_satisfies_cold_v;
using ::crucible::fixy::cipher::stance_pack_satisfies_head_v;

// `pack<Grants...>` carrier — used by the canonical stance aliases
// and exposed for callers building their own stance packs.
using ::crucible::fixy::cipher::pack;

}  // namespace crucible::fixy::wrap::cipher

// ── FIXY-V-228 — fixy/CipherDurable.h umbrella re-export ─────────────
//
// Three §XXI ctx-bound mint factories that synthesize phantom-typed
// `CipherDurableHandle<Stance>` instances corresponding to the V-227
// composite stances.  Re-exposed at fixy::wrap::cipher::durable:: so
// callers reach the §XXI mints (`mint_warm_writer` / `mint_cold_writer`
// / `mint_head_advancer`), the inherent handle type itself
// (`CipherDurableHandle`), and the three concept gates
// (`CtxFitsWarmWriterMint` / `CtxFitsColdWriterMint` /
// `CtxFitsHeadAdvancerMint`) through one short path.

#include <crucible/fixy/CipherDurable.h>    // FIXY-V-228 substrate

namespace crucible::fixy::wrap::cipher::durable {

// Stance tag types — pinned-enumerator phantom records.
using ::crucible::fixy::cipher::durable::warm_writer_stance;
using ::crucible::fixy::cipher::durable::cold_writer_stance;
using ::crucible::fixy::cipher::durable::head_advance_stance;

// CipherDurableHandle<Stance> — phantom-typed move-only RAII wrapper
// over `safety::FileHandle`.
using ::crucible::fixy::cipher::durable::CipherDurableHandle;

// Ctx-bound mint concept gates — single-clause `requires` for the
// §XXI factories.  Each folds CtxAdmitsIoBlock + three extras-do-not-
// engage-stance-pinned-axis predicates.
using ::crucible::fixy::cipher::durable::CtxFitsWarmWriterMint;
using ::crucible::fixy::cipher::durable::CtxFitsColdWriterMint;
using ::crucible::fixy::cipher::durable::CtxFitsHeadAdvancerMint;

// §XXI mint factories — `[[nodiscard]] noexcept` (drop `constexpr`
// per the syscall carve-out).  Each returns
// `expected<Linear<CipherDurableHandle<Stance>>, error_code>`.
using ::crucible::fixy::cipher::durable::mint_warm_writer;
using ::crucible::fixy::cipher::durable::mint_cold_writer;
using ::crucible::fixy::cipher::durable::mint_head_advancer;

}  // namespace crucible::fixy::wrap::cipher::durable

// ─── Dual-export sentinel — FIXY-U-020 (#1732) ─────────────────────
//
// Header-internal static_asserts pin each `using ::crucible::safety::X`
// alias to its substrate origin via `std::is_same_v`.  The companion
// reach test (test/test_fixy_umbrella.cpp::reach_sub_namespaces) only
// witnesses that `fixy::wrap::X` is REACHABLE — it does NOT verify the
// underlying type identity.  A typo that aliases `fixy::wrap::Linear`
// to the wrong substrate symbol (e.g., `using detail::Linear;` instead
// of `using safety::Linear;`) would slip past the reach test.  This
// sentinel block catches that drift in the header itself, before any
// caller TU compiles.
//
// Coverage: 11 Graded-backed wrappers (CLAUDE.md §XVI canonical) +
// 5 Mutation-derivative wrappers + 3 representative predicate lambdas
// + the 2 fundamental Refined aliases (Linear/Refined composition).
// 21 cells total — covers every conceptual category in this header.
//
// The predicate-lambda check uses `decltype(positive) ==
// decltype(safety::positive)` because lambdas are unnameable types;
// the address-equality form `&positive == &safety::positive` is not a
// constant expression for stateless lambdas in C++26 unless the lambda
// is `consteval`.  decltype-equality is the structural witness that
// the two names resolve to the SAME compile-time lambda type.

namespace crucible::fixy::wrap::self_test {

// Tier-S Graded wrappers (11 per CLAUDE.md §XVI canonical outer→inner).
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::Linear<int>,
    ::crucible::safety::Linear<int>>,
    "fixy::wrap::Linear must alias safety::Linear — dual-export drift.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::Refined<::crucible::safety::positive, int>,
    ::crucible::safety::Refined<::crucible::safety::positive, int>>,
    "fixy::wrap::Refined must alias safety::Refined — dual-export drift.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::SealedRefined<::crucible::safety::positive, int>,
    ::crucible::safety::SealedRefined<::crucible::safety::positive, int>>,
    "fixy::wrap::SealedRefined must alias safety::SealedRefined.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::Tagged<int, ::crucible::safety::source::FromUser>,
    ::crucible::safety::Tagged<int, ::crucible::safety::source::FromUser>>,
    "fixy::wrap::Tagged must alias safety::Tagged.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::Secret<int>,
    ::crucible::safety::Secret<int>>,
    "fixy::wrap::Secret must alias safety::Secret.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::Monotonic<std::uint64_t>,
    ::crucible::safety::Monotonic<std::uint64_t>>,
    "fixy::wrap::Monotonic must alias safety::Monotonic.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::AppendOnly<int>,
    ::crucible::safety::AppendOnly<int>>,
    "fixy::wrap::AppendOnly must alias safety::AppendOnly.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::Stale<int>,
    ::crucible::safety::Stale<int>>,
    "fixy::wrap::Stale must alias safety::Stale.");

// Refined composition orderings (both directions must alias correctly).
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::LinearRefined<::crucible::safety::positive, int>,
    ::crucible::safety::LinearRefined<::crucible::safety::positive, int>>,
    "fixy::wrap::LinearRefined must alias safety::LinearRefined.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::RefinedLinear<::crucible::safety::positive, int>,
    ::crucible::safety::RefinedLinear<::crucible::safety::positive, int>>,
    "fixy::wrap::RefinedLinear must alias safety::RefinedLinear.");

// FIXY-U-159b — dual-export sentinels for the three §XVI named
// aliases added in U-159 (NonZero / NonEmpty / NonEmptySpan).  Match
// the dual-export discipline of Refined/LinearRefined/RefinedLinear
// above: a future drift in fixy/wrap/Refined.h's using-declarations
// must red the build at this static_assert, NOT silently at a
// production call site.
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::NonZero<int>,
    ::crucible::safety::NonZero<int>>,
    "fixy::wrap::NonZero must alias safety::NonZero — dual-export drift.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::NonEmpty<std::span<int>>,
    ::crucible::safety::NonEmpty<std::span<int>>>,
    "fixy::wrap::NonEmpty must alias safety::NonEmpty — dual-export drift.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::NonEmptySpan<int>,
    ::crucible::safety::NonEmptySpan<int>>,
    "fixy::wrap::NonEmptySpan must alias safety::NonEmptySpan — dual-export drift.");

// FIXY-U-160 — dual-export sentinels for the two §XVI parameterised
// named aliases.  Witness at three parameter cardinalities each
// (catches a future drift that breaks only one specialisation, e.g.
// N=0 if someone adds a `requires (N >= 1)` clause to the alias).
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::MinLength<1, std::span<int>>,
    ::crucible::safety::MinLength<1, std::span<int>>>,
    "fixy::wrap::MinLength<1, ...> must alias safety::MinLength<1, ...> "
    "— dual-export drift on the §XVI parameterised length_ge surface.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::MinLength<8, std::span<int>>,
    ::crucible::safety::MinLength<8, std::span<int>>>,
    "fixy::wrap::MinLength<8, ...> must alias safety::MinLength<8, ...> "
    "— witness propagation through parameter pack at a non-trivial N.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::MaxBounded<255u, unsigned int>,
    ::crucible::safety::MaxBounded<255u, unsigned int>>,
    "fixy::wrap::MaxBounded<255u, ...> must alias safety::MaxBounded<255u, ...> "
    "— dual-export drift on the §XVI parameterised bounded_above surface.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::MaxBounded<128u, unsigned int>,
    ::crucible::safety::MaxBounded<128u, unsigned int>>,
    "fixy::wrap::MaxBounded<128u, ...> must alias safety::MaxBounded<128u, ...> "
    "— witness propagation through parameter pack at a saturation-counter N.");

// FIXY-U-161 — dual-export sentinels for the §XVI parameterised-alias
// closure (AlignedTo + WithinRange).  Witness at two cardinalities
// each to catch a future drift that breaks only one specialisation.
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::AlignedTo<64, int*>,
    ::crucible::safety::AlignedTo<64, int*>>,
    "fixy::wrap::AlignedTo<64, ...> must alias safety::AlignedTo<64, ...> "
    "— cache-line-aligned cardinality witness.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::AlignedTo<4096, void*>,
    ::crucible::safety::AlignedTo<4096, void*>>,
    "fixy::wrap::AlignedTo<4096, ...> must alias safety::AlignedTo<4096, ...> "
    "— page-aligned cardinality witness.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::WithinRange<0, 100, int>,
    ::crucible::safety::WithinRange<0, 100, int>>,
    "fixy::wrap::WithinRange<0, 100, ...> must alias safety::WithinRange<0, 100, ...> "
    "— closed-interval cardinality witness.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::WithinRange<-128, 127, std::int8_t>,
    ::crucible::safety::WithinRange<-128, 127, std::int8_t>>,
    "fixy::wrap::WithinRange<-128, 127, ...> must alias safety::WithinRange<-128, 127, ...> "
    "— signed NTTP witness, covers the int8_t representable-range case.");

// SharedPermission — dual-exported in both fixy::wrap:: and fixy::perm::.
// Both paths MUST resolve to the same substrate type (fixy-A4-011).
struct WrapDualExportTag {};
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::SharedPermission<WrapDualExportTag>,
    ::crucible::safety::SharedPermission<WrapDualExportTag>>,
    "fixy::wrap::SharedPermission must alias safety::SharedPermission "
    "— this dual-export must agree with the fixy::perm:: parallel path.");

// Mutation.h derivatives.
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::WriteOnce<int>,
    ::crucible::safety::WriteOnce<int>>,
    "fixy::wrap::WriteOnce must alias safety::WriteOnce.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::AtomicMonotonic<std::uint64_t>,
    ::crucible::safety::AtomicMonotonic<std::uint64_t>>,
    "fixy::wrap::AtomicMonotonic must alias safety::AtomicMonotonic.");

// Predicate lambdas — decltype-identity (lambdas are unnameable, so
// is_same_v on decltype is the structural witness).
static_assert(std::is_same_v<
    decltype(::crucible::fixy::wrap::positive),
    decltype(::crucible::safety::positive)>,
    "fixy::wrap::positive must alias safety::positive — predicate drift.");
static_assert(std::is_same_v<
    decltype(::crucible::fixy::wrap::non_negative),
    decltype(::crucible::safety::non_negative)>,
    "fixy::wrap::non_negative must alias safety::non_negative.");
static_assert(std::is_same_v<
    decltype(::crucible::fixy::wrap::non_null),
    decltype(::crucible::safety::non_null)>,
    "fixy::wrap::non_null must alias safety::non_null.");

// ─── Tier-S canonical outer→inner (11 cells, FIXY-U-010) ──────────
// Each canonical wrapper is asserted with a concrete grade pick so the
// substrate's lattice-tier enum AND the wrapper class template both
// participate in the type identity.  A drift on either rail (wrong
// enum, wrong substrate parameter order) reds the build at the header.

static_assert(std::is_same_v<
    ::crucible::fixy::wrap::HotPath<::crucible::fixy::wrap::HotPathTier_v::Hot, int>,
    ::crucible::safety::HotPath<::crucible::safety::HotPathTier_v::Hot, int>>,
    "fixy::wrap::HotPath must alias safety::HotPath.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::DetSafe<::crucible::fixy::wrap::DetSafeTier_v::Pure, int>,
    ::crucible::safety::DetSafe<::crucible::safety::DetSafeTier_v::Pure, int>>,
    "fixy::wrap::DetSafe must alias safety::DetSafe.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::NumericalTier<::crucible::fixy::wrap::Tolerance::BITEXACT, int>,
    ::crucible::safety::NumericalTier<::crucible::safety::Tolerance::BITEXACT, int>>,
    "fixy::wrap::NumericalTier must alias safety::NumericalTier.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::Vendor<::crucible::fixy::wrap::VendorBackend_v::Portable, int>,
    ::crucible::safety::Vendor<::crucible::safety::VendorBackend_v::Portable, int>>,
    "fixy::wrap::Vendor must alias safety::Vendor.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::ResidencyHeat<::crucible::fixy::wrap::ResidencyHeatTag_v::Hot, int>,
    ::crucible::safety::ResidencyHeat<::crucible::safety::ResidencyHeatTag_v::Hot, int>>,
    "fixy::wrap::ResidencyHeat must alias safety::ResidencyHeat.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::CipherTier<::crucible::fixy::wrap::CipherTierTag_v::Hot, int>,
    ::crucible::safety::CipherTier<::crucible::safety::CipherTierTag_v::Hot, int>>,
    "fixy::wrap::CipherTier must alias safety::CipherTier.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::AllocClass<::crucible::fixy::wrap::AllocClassTag_v::Arena, int>,
    ::crucible::safety::AllocClass<::crucible::safety::AllocClassTag_v::Arena, int>>,
    "fixy::wrap::AllocClass must alias safety::AllocClass.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::Wait<::crucible::fixy::wrap::WaitStrategy_v::SpinPause, int>,
    ::crucible::safety::Wait<::crucible::safety::WaitStrategy_v::SpinPause, int>>,
    "fixy::wrap::Wait must alias safety::Wait.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::MemOrder<::crucible::fixy::wrap::MemOrderTag_v::AcqRel, int>,
    ::crucible::safety::MemOrder<::crucible::safety::MemOrderTag_v::AcqRel, int>>,
    "fixy::wrap::MemOrder must alias safety::MemOrder.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::Progress<::crucible::fixy::wrap::ProgressClass_v::Bounded, int>,
    ::crucible::safety::Progress<::crucible::safety::ProgressClass_v::Bounded, int>>,
    "fixy::wrap::Progress must alias safety::Progress.");

// Computation — innermost canonical (effects:: namespace).
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::Computation<::crucible::effects::Row<>, int>,
    ::crucible::effects::Computation<::crucible::effects::Row<>, int>>,
    "fixy::wrap::Computation must alias effects::Computation.");

// ─── Off-tree Graded wrappers (7 cells, FIXY-U-010) ──────────────

static_assert(std::is_same_v<
    ::crucible::fixy::wrap::Consistency<::crucible::fixy::wrap::Consistency_v::STRONG, int>,
    ::crucible::safety::Consistency<::crucible::safety::Consistency_v::STRONG, int>>,
    "fixy::wrap::Consistency must alias safety::Consistency.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::OpaqueLifetime<::crucible::fixy::wrap::Lifetime_v::PER_PROGRAM, int>,
    ::crucible::safety::OpaqueLifetime<::crucible::safety::Lifetime_v::PER_PROGRAM, int>>,
    "fixy::wrap::OpaqueLifetime must alias safety::OpaqueLifetime.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::Crash<::crucible::fixy::wrap::CrashClass_v::NoThrow, int>,
    ::crucible::safety::Crash<::crucible::safety::CrashClass_v::NoThrow, int>>,
    "fixy::wrap::Crash must alias safety::Crash.");

// FinalBy<T> + Wait sentinel — Crash sub-namespace `crash::NoThrow` is
// the user-facing per-tier alias.  Just ensure the class + tier-enum
// chain through fixy::wrap:: is a single substrate type (above test).
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::Budgeted<int>,
    ::crucible::safety::Budgeted<int>>,
    "fixy::wrap::Budgeted must alias safety::Budgeted.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::EpochVersioned<int>,
    ::crucible::safety::EpochVersioned<int>>,
    "fixy::wrap::EpochVersioned must alias safety::EpochVersioned.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::NumaPlacement<int>,
    ::crucible::safety::NumaPlacement<int>>,
    "fixy::wrap::NumaPlacement must alias safety::NumaPlacement.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::RecipeSpec<int>,
    ::crucible::safety::RecipeSpec<int>>,
    "fixy::wrap::RecipeSpec must alias safety::RecipeSpec.");

// FIXY-V-056 — Witness alias sentinel (one tier per cell, exhaustive
// across the 4-element WitnessLattice chain; per-tier convenience
// aliases checked in the second block below).
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::Witness<
        ::crucible::fixy::wrap::Witness_v::UNWITNESSED, int>,
    ::crucible::safety::Witness<
        ::crucible::safety::Witness_v::UNWITNESSED, int>>,
    "fixy::wrap::Witness must alias safety::Witness.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::Witness<
        ::crucible::fixy::wrap::Witness_v::TYPE_CHECKED, int>,
    ::crucible::safety::Witness<
        ::crucible::safety::Witness_v::TYPE_CHECKED, int>>,
    "fixy::wrap::Witness must alias safety::Witness (TYPE_CHECKED).");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::Witness<
        ::crucible::fixy::wrap::Witness_v::TEST_PASSED, int>,
    ::crucible::safety::Witness<
        ::crucible::safety::Witness_v::TEST_PASSED, int>>,
    "fixy::wrap::Witness must alias safety::Witness (TEST_PASSED).");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::Witness<
        ::crucible::fixy::wrap::Witness_v::FORMALLY_VERIFIED, int>,
    ::crucible::safety::Witness<
        ::crucible::safety::Witness_v::FORMALLY_VERIFIED, int>>,
    "fixy::wrap::Witness must alias safety::Witness (FORMALLY_VERIFIED).");

// Per-tier convenience aliases — verify each one reaches the matching
// safety::Witness<Tier, T> after substitution.  Closes the surface so
// `fixy::wrap::FormallyVerified<int>` and `safety::Witness<FORMALLY_VERIFIED, int>`
// are the same type at every consumer site.
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::Unwitnessed<int>,
    ::crucible::safety::Witness<
        ::crucible::safety::Witness_v::UNWITNESSED, int>>,
    "fixy::wrap::Unwitnessed must alias Witness<UNWITNESSED, T>.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::TypeChecked<int>,
    ::crucible::safety::Witness<
        ::crucible::safety::Witness_v::TYPE_CHECKED, int>>,
    "fixy::wrap::TypeChecked must alias Witness<TYPE_CHECKED, T>.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::TestPassed<int>,
    ::crucible::safety::Witness<
        ::crucible::safety::Witness_v::TEST_PASSED, int>>,
    "fixy::wrap::TestPassed must alias Witness<TEST_PASSED, T>.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::FormallyVerified<int>,
    ::crucible::safety::Witness<
        ::crucible::safety::Witness_v::FORMALLY_VERIFIED, int>>,
    "fixy::wrap::FormallyVerified must alias Witness<FORMALLY_VERIFIED, T>.");

// mint_witness §XXI factory reach via qualified-id decltype identity
// (same pattern as mint_view / mint_sealed_refined / mint_tagged).
static_assert(std::is_same_v<
    decltype(&::crucible::fixy::wrap::mint_witness<
        ::crucible::fixy::wrap::Witness_v::FORMALLY_VERIFIED, int, int>),
    decltype(&::crucible::safety::mint_witness<
        ::crucible::safety::Witness_v::FORMALLY_VERIFIED, int, int>)>,
    "FIXY-V-056: fixy::wrap::mint_witness must alias safety::mint_witness.");

// Hw<HwInstruction_v, T> base-type identity — Tier-S hardware band
// re-export (this commit).  Pins the fixy surface to safety::Hw.
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::Hw<
        ::crucible::fixy::wrap::HwInstruction_v::Vectorizable, int>,
    ::crucible::safety::Hw<
        ::crucible::safety::HwInstruction_v::Vectorizable, int>>,
    "fixy::wrap::Hw must alias safety::Hw.");

// mint_hw §XXI factory reach via qualified-id decltype identity —
// same pattern as mint_witness / mint_affine.  Closes the mint_hw
// `[✗ NO-FIXY]` inventory marker: a refactor of safety::mint_hw's
// signature reds this cell at the fixy boundary.
static_assert(std::is_same_v<
    decltype(&::crucible::fixy::wrap::mint_hw<
        ::crucible::fixy::wrap::HwInstruction_v::Vectorizable, int, int>),
    decltype(&::crucible::safety::mint_hw<
        ::crucible::safety::HwInstruction_v::Vectorizable, int, int>)>,
    "fixy::wrap::mint_hw must alias safety::mint_hw.");

// BarrierGuarded<BarrierStrength_v, T> base-type identity — Tier-S
// memory-barrier band re-export (this commit).
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::BarrierGuarded<
        ::crucible::fixy::wrap::BarrierStrength_v::SeqCst, int>,
    ::crucible::safety::BarrierGuarded<
        ::crucible::safety::BarrierStrength_v::SeqCst, int>>,
    "fixy::wrap::BarrierGuarded must alias safety::BarrierGuarded.");

// mint_barrier_guarded §XXI factory reach via qualified-id decltype
// identity — same pattern as mint_hw.  Closes the
// mint_barrier_guarded `[✗ NO-FIXY]` inventory marker.
static_assert(std::is_same_v<
    decltype(&::crucible::fixy::wrap::mint_barrier_guarded<
        ::crucible::fixy::wrap::BarrierStrength_v::SeqCst, int, int>),
    decltype(&::crucible::safety::mint_barrier_guarded<
        ::crucible::safety::BarrierStrength_v::SeqCst, int, int>)>,
    "fixy::wrap::mint_barrier_guarded must alias safety::mint_barrier_guarded.");

// SimdWidthPinned<SimdIsa_v, T> base-type identity — Tier-S SIMD-ISA
// band re-export (this commit).
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::SimdWidthPinned<
        ::crucible::fixy::wrap::SimdIsa_v::Avx512Bw, int>,
    ::crucible::safety::SimdWidthPinned<
        ::crucible::safety::SimdIsa_v::Avx512Bw, int>>,
    "fixy::wrap::SimdWidthPinned must alias safety::SimdWidthPinned.");

// mint_simd_width_pinned §XXI factory reach via qualified-id decltype
// identity — same pattern as mint_hw.  Closes the
// mint_simd_width_pinned `[✗ NO-FIXY]` inventory marker.
static_assert(std::is_same_v<
    decltype(&::crucible::fixy::wrap::mint_simd_width_pinned<
        ::crucible::fixy::wrap::SimdIsa_v::Avx512Bw, int, int>),
    decltype(&::crucible::safety::mint_simd_width_pinned<
        ::crucible::safety::SimdIsa_v::Avx512Bw, int, int>)>,
    "fixy::wrap::mint_simd_width_pinned must alias safety::mint_simd_width_pinned.");

// JoinPolicy<JoinPolicy_v, T> base-type identity — Tier-S thread-join
// band re-export (this commit).
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::JoinPolicy<
        ::crucible::fixy::wrap::JoinPolicy_v::JOIN_ALL, int>,
    ::crucible::safety::JoinPolicy<
        ::crucible::safety::JoinPolicy_v::JOIN_ALL, int>>,
    "fixy::wrap::JoinPolicy must alias safety::JoinPolicy.");

// mint_join_policy §XXI factory reach via qualified-id decltype
// identity — same pattern as mint_hw.  Closes the mint_join_policy
// `[✗ NO-FIXY]` inventory marker.
static_assert(std::is_same_v<
    decltype(&::crucible::fixy::wrap::mint_join_policy<
        ::crucible::fixy::wrap::JoinPolicy_v::JOIN_ALL, int, int>),
    decltype(&::crucible::safety::mint_join_policy<
        ::crucible::safety::JoinPolicy_v::JOIN_ALL, int, int>)>,
    "fixy::wrap::mint_join_policy must alias safety::mint_join_policy.");

// SuspendBehavior<SuspendBehavior_v, T> base-type identity — Tier-S
// suspend-behavior band re-export (this commit).
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::SuspendBehavior<
        ::crucible::fixy::wrap::SuspendBehavior_v::KeepsTicking, int>,
    ::crucible::safety::SuspendBehavior<
        ::crucible::safety::SuspendBehavior_v::KeepsTicking, int>>,
    "fixy::wrap::SuspendBehavior must alias safety::SuspendBehavior.");

// mint_suspend_behavior §XXI factory reach via qualified-id decltype
// identity — same pattern as mint_hw.  Closes the
// mint_suspend_behavior `[✗ NO-FIXY]` inventory marker.
static_assert(std::is_same_v<
    decltype(&::crucible::fixy::wrap::mint_suspend_behavior<
        ::crucible::fixy::wrap::SuspendBehavior_v::KeepsTicking, int, int>),
    decltype(&::crucible::safety::mint_suspend_behavior<
        ::crucible::safety::SuspendBehavior_v::KeepsTicking, int, int>)>,
    "fixy::wrap::mint_suspend_behavior must alias safety::mint_suspend_behavior.");

// FIXY-V-058 — Affine alias sentinel (substrate ships in V-057;
// fixy::wrap:: surface is the V-058 re-export).
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::Affine<int>,
    ::crucible::safety::Affine<int>>,
    "fixy::wrap::Affine must alias safety::Affine.");

// mint_affine §XXI factory reach via qualified-id decltype identity —
// same pattern as mint_linear / mint_witness / mint_view / mint_sealed_refined.
// Pins the fixy surface to the substrate factory: a refactor of
// safety::mint_affine's signature reds this cell at the fixy boundary.
static_assert(std::is_same_v<
    decltype(&::crucible::fixy::wrap::mint_affine<int, int>),
    decltype(&::crucible::safety::mint_affine<int, int>)>,
    "FIXY-V-058: fixy::wrap::mint_affine must alias safety::mint_affine.");

// is_already_consume_disciplined trait reach — proves the rejection
// table referenced by Affine's class-body static_assert is reachable
// from the fixy surface (so consumers can witness the trait without
// descending into safety/).  Permission/SharedPermission are tagged
// true; plain T is false.
static_assert(::crucible::fixy::wrap::is_already_consume_disciplined_v<int> ==
              ::crucible::safety::is_already_consume_disciplined_v<int>,
    "FIXY-V-058: fixy::wrap::is_already_consume_disciplined_v must "
    "alias safety::is_already_consume_disciplined_v.");

// ─── Structural wrappers (7 cells, FIXY-U-010) ────────────────────

struct WrapStructuralTag {};
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::Pinned<int>,
    ::crucible::safety::Pinned<int>>,
    "fixy::wrap::Pinned must alias safety::Pinned.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::NonMovable<int>,
    ::crucible::safety::NonMovable<int>>,
    "fixy::wrap::NonMovable must alias safety::NonMovable.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::ScopedView<int, WrapStructuralTag>,
    ::crucible::safety::ScopedView<int, WrapStructuralTag>>,
    "fixy::wrap::ScopedView must alias safety::ScopedView.");
// mint_view — function-template identity by qualified-id decltype.
static_assert(std::is_same_v<
    decltype(&::crucible::fixy::wrap::mint_view<
        WrapStructuralTag, int>),
    decltype(&::crucible::safety::mint_view<
        WrapStructuralTag, int>)>,
    "fixy::wrap::mint_view must alias safety::mint_view.");
// mint_sealed_refined / mint_tagged — FIXY-U-115 §XXI surface (mint_refined
// reach is proven in fixy/wrap/Refined.h::self_test, co-located with its
// granular using-decl).
static_assert(std::is_same_v<
    decltype(&::crucible::fixy::wrap::mint_sealed_refined<
        ::crucible::safety::positive, int>),
    decltype(&::crucible::safety::mint_sealed_refined<
        ::crucible::safety::positive, int>)>,
    "FIXY-U-115: fixy::wrap::mint_sealed_refined must alias safety::mint_sealed_refined.");
static_assert(std::is_same_v<
    decltype(&::crucible::fixy::wrap::mint_tagged<
        ::crucible::safety::source::FromUser, int>),
    decltype(&::crucible::safety::mint_tagged<
        ::crucible::safety::source::FromUser, int>)>,
    "FIXY-U-115: fixy::wrap::mint_tagged must alias safety::mint_tagged.");
// no_scoped_view_field_check — function-template identity.
static_assert(std::is_same_v<
    decltype(&::crucible::fixy::wrap::no_scoped_view_field_check<int>),
    decltype(&::crucible::safety::no_scoped_view_field_check<int>)>,
    "fixy::wrap::no_scoped_view_field_check must alias safety::"
    "no_scoped_view_field_check.");
// IsBorrowedRef — concept identity via positive + negative witness.
static_assert(::crucible::fixy::wrap::IsBorrowedRef<
    ::crucible::safety::BorrowedRef<int>>,
    "fixy::wrap::IsBorrowedRef must accept safety::BorrowedRef<int>.");
static_assert(!::crucible::fixy::wrap::IsBorrowedRef<int>,
    "fixy::wrap::IsBorrowedRef must reject plain int.");

// FIXY-V-035 — SwmrReader (function-ptr) + IsSwmrReader (handle) sentinels.
//
// Two concept surfaces — two pairs of positive + negative witnesses.  The
// synthetic types mirror the substrate's own self-test in IsSwmrHandle.h
// so reviewers can cross-check shape without leaving the fixy:: umbrella.
namespace fixy_v_035_swmr_witness {
struct ReaderHandle {
    [[nodiscard]] int load() const noexcept { return 0; }
};
struct WriterHandle {
    void publish(int const&) noexcept {}
};
// Function whose first parameter is a SWMR-reader-handle rvalue-ref and
// whose return is by-value non-OwnedRegion non-reference.
inline int read_int(ReaderHandle&&) noexcept { return 0; }
// Negative: parameter is not a SWMR reader handle (plain int).
inline int read_from_int(int&&) noexcept { return 0; }
}  // namespace fixy_v_035_swmr_witness

// IsSwmrReader<T> — handle-shape concept: positive + negative.
static_assert(::crucible::fixy::wrap::IsSwmrReader<
    fixy_v_035_swmr_witness::ReaderHandle>,
    "FIXY-V-035: fixy::wrap::IsSwmrReader must accept a handle with "
    "[[nodiscard]] T load() const noexcept.");
static_assert(!::crucible::fixy::wrap::IsSwmrReader<
    fixy_v_035_swmr_witness::WriterHandle>,
    "FIXY-V-035: fixy::wrap::IsSwmrReader must reject a writer handle "
    "(no load() — would match IsSwmrWriter instead).");
static_assert(!::crucible::fixy::wrap::IsSwmrReader<int>,
    "FIXY-V-035: fixy::wrap::IsSwmrReader must reject plain int.");

// is_swmr_reader_v<T> — variable-template surface co-tracks the concept.
static_assert(::crucible::fixy::wrap::is_swmr_reader_v<
    fixy_v_035_swmr_witness::ReaderHandle>,
    "FIXY-V-035: fixy::wrap::is_swmr_reader_v must agree with IsSwmrReader.");

// swmr_reader_value_t<T> — payload extraction must propagate.
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::swmr_reader_value_t<
        fixy_v_035_swmr_witness::ReaderHandle>,
    int>,
    "FIXY-V-035: fixy::wrap::swmr_reader_value_t must alias the handle's "
    "load() return type.");

// SwmrReader<auto FnPtr> — function-pointer-shape concept: positive + negative.
static_assert(::crucible::fixy::wrap::SwmrReader<
    &fixy_v_035_swmr_witness::read_int>,
    "FIXY-V-035: fixy::wrap::SwmrReader must accept a function with one "
    "SWMR-reader-handle rvalue-ref parameter and a by-value non-region return.");
static_assert(!::crucible::fixy::wrap::SwmrReader<
    &fixy_v_035_swmr_witness::read_from_int>,
    "FIXY-V-035: fixy::wrap::SwmrReader must reject a function whose "
    "param 0 is not a SWMR reader handle (int&& fails the handle predicate).");

// is_swmr_reader_function_v<auto FnPtr> — variable-template co-tracks.
static_assert(::crucible::fixy::wrap::is_swmr_reader_function_v<
    &fixy_v_035_swmr_witness::read_int>,
    "FIXY-V-035: fixy::wrap::is_swmr_reader_function_v must agree with SwmrReader.");

// swmr_reader_handle_value_t / swmr_reader_returned_value_t /
// swmr_reader_value_consistent_v — three extractors gated on SwmrReader.
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::swmr_reader_handle_value_t<
        &fixy_v_035_swmr_witness::read_int>,
    int>,
    "FIXY-V-035: swmr_reader_handle_value_t must extract handle payload type.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::swmr_reader_returned_value_t<
        &fixy_v_035_swmr_witness::read_int>,
    int>,
    "FIXY-V-035: swmr_reader_returned_value_t must extract function return type.");
static_assert(::crucible::fixy::wrap::swmr_reader_value_consistent_v<
    &fixy_v_035_swmr_witness::read_int>,
    "FIXY-V-035: swmr_reader_value_consistent_v must witness handle/return agreement.");

// FIXY-V-036 — SwmrWriter (function-ptr) + IsSwmrWriter (handle) sentinels.
// Companion to FIXY-V-035 reader witness; reuses ReaderHandle/WriterHandle
// from the V-035 namespace and adds writer-side function-pointer fixtures.
namespace fixy_v_036_swmr_writer_witness {
// Function whose first parameter is a SWMR-writer-handle rvalue-ref AND
// whose second parameter is the value-to-publish by value (NOT reference,
// NOT OwnedRegion) AND whose return is void.
inline void publish_int(
    fixy_v_035_swmr_witness::WriterHandle&&, int) noexcept {}
// Negative: param 0 is not a SWMR writer handle (plain int).
inline void publish_from_int(int&&, int) noexcept {}
// Negative: param 1 is by lvalue reference (writer expects by-value).
inline void publish_by_lvalue_ref(
    fixy_v_035_swmr_witness::WriterHandle&&, int&) noexcept {}
// Negative: return is non-void (writer is "publish and forget").
inline int publish_returns_int(
    fixy_v_035_swmr_witness::WriterHandle&&, int) noexcept { return 0; }
}  // namespace fixy_v_036_swmr_writer_witness

// IsSwmrWriter<T> — handle-shape concept: positive + negative.
static_assert(::crucible::fixy::wrap::IsSwmrWriter<
    fixy_v_035_swmr_witness::WriterHandle>,
    "FIXY-V-036: fixy::wrap::IsSwmrWriter must accept a handle with "
    "void publish(T const&) noexcept.");
static_assert(!::crucible::fixy::wrap::IsSwmrWriter<
    fixy_v_035_swmr_witness::ReaderHandle>,
    "FIXY-V-036: fixy::wrap::IsSwmrWriter must reject a reader handle "
    "(no publish() — would match IsSwmrReader instead).");
static_assert(!::crucible::fixy::wrap::IsSwmrWriter<int>,
    "FIXY-V-036: fixy::wrap::IsSwmrWriter must reject plain int.");

// is_swmr_writer_v<T> — variable-template surface co-tracks the concept.
static_assert(::crucible::fixy::wrap::is_swmr_writer_v<
    fixy_v_035_swmr_witness::WriterHandle>,
    "FIXY-V-036: fixy::wrap::is_swmr_writer_v must agree with IsSwmrWriter.");

// swmr_writer_value_t<T> — payload extraction must propagate.
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::swmr_writer_value_t<
        fixy_v_035_swmr_witness::WriterHandle>,
    int>,
    "FIXY-V-036: fixy::wrap::swmr_writer_value_t must alias the handle's "
    "publish() parameter type.");

// SwmrWriter<auto FnPtr> — function-pointer-shape concept: positive + negatives.
static_assert(::crucible::fixy::wrap::SwmrWriter<
    &fixy_v_036_swmr_writer_witness::publish_int>,
    "FIXY-V-036: fixy::wrap::SwmrWriter must accept a function with "
    "(WriterHandle&&, T) → void shape.");
static_assert(!::crucible::fixy::wrap::SwmrWriter<
    &fixy_v_036_swmr_writer_witness::publish_from_int>,
    "FIXY-V-036: fixy::wrap::SwmrWriter must reject a function whose "
    "param 0 is not a SWMR writer handle.");
static_assert(!::crucible::fixy::wrap::SwmrWriter<
    &fixy_v_036_swmr_writer_witness::publish_by_lvalue_ref>,
    "FIXY-V-036: fixy::wrap::SwmrWriter must reject a function whose "
    "param 1 is by lvalue reference (writer expects by-value).");
static_assert(!::crucible::fixy::wrap::SwmrWriter<
    &fixy_v_036_swmr_writer_witness::publish_returns_int>,
    "FIXY-V-036: fixy::wrap::SwmrWriter must reject a function whose "
    "return is non-void (writer is publish-and-forget).");

// is_swmr_writer_function_v<auto FnPtr> — variable-template co-tracks.
static_assert(::crucible::fixy::wrap::is_swmr_writer_function_v<
    &fixy_v_036_swmr_writer_witness::publish_int>,
    "FIXY-V-036: fixy::wrap::is_swmr_writer_function_v must agree with SwmrWriter.");

// swmr_writer_handle_value_t / swmr_writer_published_value_t /
// swmr_writer_value_consistent_v — three extractors gated on SwmrWriter.
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::swmr_writer_handle_value_t<
        &fixy_v_036_swmr_writer_witness::publish_int>,
    int>,
    "FIXY-V-036: swmr_writer_handle_value_t must extract handle payload type.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::swmr_writer_published_value_t<
        &fixy_v_036_swmr_writer_witness::publish_int>,
    int>,
    "FIXY-V-036: swmr_writer_published_value_t must extract param 1 value type.");
static_assert(::crucible::fixy::wrap::swmr_writer_value_consistent_v<
    &fixy_v_036_swmr_writer_witness::publish_int>,
    "FIXY-V-036: swmr_writer_value_consistent_v must witness handle/value agreement.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::OwnedRegion<int, WrapStructuralTag>,
    ::crucible::safety::OwnedRegion<int, WrapStructuralTag>>,
    "fixy::wrap::OwnedRegion must alias safety::OwnedRegion.");
// NotInherited is a `concept`, not a class — concept-identity is by
// structural matching; the using-declaration brings the name into
// fixy::wrap::, and we anchor reach by witnessing that the concept
// is satisfied by an alias of a final type in both namespaces.
struct WrapFinalT final {};
static_assert(::crucible::fixy::wrap::NotInherited<WrapFinalT>,
    "fixy::wrap::NotInherited must alias safety::NotInherited concept.");
static_assert(::crucible::safety::NotInherited<WrapFinalT>,
    "Sentinel: NotInherited concept must accept a `final` class.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::FinalBy<WrapFinalT>,
    ::crucible::safety::FinalBy<WrapFinalT>>,
    "fixy::wrap::FinalBy must alias safety::FinalBy.");

// ── FIXY-V-178 — signature_traits family dual-export sentinels ─────
//
// Inline sample whose signature is known at authoring time: arity 2,
// (int, double) -> int, noexcept.  An inline namespace-scope function
// has external linkage so `&v178_sig_sample` is a valid NTTP and the
// same entity across every TU that includes Wrap.h (no ODR drift).
inline int v178_sig_sample(int, double) noexcept { return 0; }

static_assert(std::is_same_v<
    ::crucible::fixy::wrap::signature_traits<&v178_sig_sample>,
    ::crucible::safety::extract::signature_traits<&v178_sig_sample>>,
    "fixy::wrap::signature_traits must alias safety::extract::signature_traits.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::param_type_t<&v178_sig_sample, 0>,
    ::crucible::safety::extract::param_type_t<&v178_sig_sample, 0>>,
    "fixy::wrap::param_type_t must alias the substrate.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::return_type_t<&v178_sig_sample>,
    ::crucible::safety::extract::return_type_t<&v178_sig_sample>>,
    "fixy::wrap::return_type_t must alias the substrate.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::function_type_t<&v178_sig_sample>,
    ::crucible::safety::extract::function_type_t<&v178_sig_sample>>,
    "fixy::wrap::function_type_t must alias the substrate.");
static_assert(
    ::crucible::fixy::wrap::arity_v<&v178_sig_sample> ==
    ::crucible::safety::extract::arity_v<&v178_sig_sample>,
    "fixy::wrap::arity_v must alias the substrate.");
static_assert(
    ::crucible::fixy::wrap::is_noexcept_v<&v178_sig_sample> ==
    ::crucible::safety::extract::is_noexcept_v<&v178_sig_sample>,
    "fixy::wrap::is_noexcept_v must alias the substrate.");
// Value anchors — pin the sample's known shape so a drifted re-export
// to a DIFFERENT FnPtr's traits is caught, not just a name typo.
static_assert(::crucible::fixy::wrap::arity_v<&v178_sig_sample> == 2,
    "Sentinel: v178_sig_sample has arity 2.");
static_assert(::crucible::fixy::wrap::is_noexcept_v<&v178_sig_sample>,
    "Sentinel: v178_sig_sample is noexcept.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::return_type_t<&v178_sig_sample>, int>,
    "Sentinel: v178_sig_sample returns int.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::param_type_t<&v178_sig_sample, 0>, int>,
    "Sentinel: v178_sig_sample param 0 is int.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::param_type_t<&v178_sig_sample, 1>, double>,
    "Sentinel: v178_sig_sample param 1 is double.");

// ── FIXY-V-178 — GradedExtract family dual-export sentinels ────────
//
// Instantiated against a REAL production GradedWrapper (Linear<int>,
// regime-1) per the GradedExtract.h self-test note: synthetic
// witnesses fail the concept's CHEAT-3 forwarder-fidelity clause, so
// the dual-export check uses an already-conformant wrapper.
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::value_type_of_t<::crucible::safety::Linear<int>>,
    ::crucible::safety::extract::value_type_of_t<::crucible::safety::Linear<int>>>,
    "fixy::wrap::value_type_of_t must alias the substrate.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::lattice_of_t<::crucible::safety::Linear<int>>,
    ::crucible::safety::extract::lattice_of_t<::crucible::safety::Linear<int>>>,
    "fixy::wrap::lattice_of_t must alias the substrate.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::grade_of_t<::crucible::safety::Linear<int>>,
    ::crucible::safety::extract::grade_of_t<::crucible::safety::Linear<int>>>,
    "fixy::wrap::grade_of_t must alias the substrate.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::graded_type_of_t<::crucible::safety::Linear<int>>,
    ::crucible::safety::extract::graded_type_of_t<::crucible::safety::Linear<int>>>,
    "fixy::wrap::graded_type_of_t must alias the substrate.");
static_assert(
    ::crucible::fixy::wrap::modality_of_v<::crucible::safety::Linear<int>> ==
    ::crucible::safety::extract::modality_of_v<::crucible::safety::Linear<int>>,
    "fixy::wrap::modality_of_v must alias the substrate.");
static_assert(
    ::crucible::fixy::wrap::is_graded_wrapper_v<::crucible::safety::Linear<int>> ==
    ::crucible::safety::extract::is_graded_wrapper_v<::crucible::safety::Linear<int>>,
    "fixy::wrap::is_graded_wrapper_v must alias the substrate.");
static_assert(
    ::crucible::fixy::wrap::is_graded_specialization_v<int> ==
    ::crucible::safety::extract::is_graded_specialization_v<int>,
    "fixy::wrap::is_graded_specialization_v must alias the substrate.");
// Value anchors — Linear<int> IS a GradedWrapper; a bare int is not.
static_assert(::crucible::fixy::wrap::is_graded_wrapper_v<::crucible::safety::Linear<int>>,
    "Sentinel: Linear<int> is a GradedWrapper.");
static_assert(!::crucible::fixy::wrap::is_graded_wrapper_v<int>,
    "Sentinel: int is not a GradedWrapper.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::value_type_of_t<::crucible::safety::Linear<int>>, int>,
    "Sentinel: Linear<int> user-facing value_type is int.");

// ct:: free-function decltype identity (function templates, so the
// drift is detectable on the qualified-id type).
static_assert(std::is_same_v<
    decltype(&::crucible::fixy::wrap::ct::select<unsigned>),
    decltype(&::crucible::safety::ct::select<unsigned>)>,
    "fixy::wrap::ct::select must alias safety::ct::select.");

// Borrowed / BorrowedRef identity — exercised by FIXY-U-093 (MerkleDag.h
// trace_entry getter signatures).  Parent tag is a placeholder struct so
// the test does not pull a substrate type beyond Borrowed.h.
struct WrapBorrowedParentT {};
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::Borrowed<int, WrapBorrowedParentT>,
    ::crucible::safety::Borrowed<int, WrapBorrowedParentT>>,
    "fixy::wrap::Borrowed must alias safety::Borrowed.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::BorrowedRef<int>,
    ::crucible::safety::BorrowedRef<int>>,
    "fixy::wrap::BorrowedRef must alias safety::BorrowedRef.");

// Saturated identity — exercised by FIXY-U-093 compute_storage_nbytes.
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::Saturated<std::uint64_t>,
    ::crucible::safety::Saturated<std::uint64_t>>,
    "fixy::wrap::Saturated must alias safety::Saturated.");

// FixedArray identity — exercised by FIXY-U-096a StorageNbytes.h migration.
// FixedArray<T, N> is a non-Graded structural newtype (bounded stack-array);
// the re-export must preserve type identity bit-for-bit so the
// `alignas(64) FixedArray<int64_t, 8>` SIMD-aligned site in StorageNbytes.h
// stays layout-compatible with the substrate primary.
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::FixedArray<int, 8>,
    ::crucible::safety::FixedArray<int, 8>>,
    "fixy::wrap::FixedArray must alias safety::FixedArray.");

// Cyclic identity — exercised by the WRAP-RegionCache-4 (#989) and
// WRAP-Transaction-4 (#1063) ring-cursor migrations.  Cyclic<T, N> is a
// non-Graded structural newtype (free-running modular counter); the
// re-export must preserve type identity so a `Cyclic<uint32_t, 8>`
// head_ field stays sizeof(uint32_t)-compatible with the substrate
// primary and the masking/wrap invariant is one and the same type.
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::Cyclic<std::uint32_t, 8>,
    ::crucible::safety::Cyclic<std::uint32_t, 8>>,
    "fixy::wrap::Cyclic must alias safety::Cyclic.");

// CyclicBuffer identity — exercised by the WRAP-Transaction-4 (#1063) and
// WRAP-RegionCache-4 (#989) ring migrations.  CyclicBuffer<T, N> is a
// non-Graded structural composition (FixedArray + Cyclic +
// BoundedMonotonic); the re-export must preserve type identity so a
// `CyclicBuffer<Transaction, N>` ring field stays layout-compatible with
// the substrate primary and the claim()/recent() invariants are one type.
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::CyclicBuffer<int, 8>,
    ::crucible::safety::CyclicBuffer<int, 8>>,
    "fixy::wrap::CyclicBuffer must alias safety::CyclicBuffer.");

// WeakRef identity — exercised by the WRAP-RegionCache-1 (#986) and
// WRAP-RE-1 (#993) cache-slot / back-pointer migrations.  WeakRef<T> is
// a non-Graded structural newtype (nullable non-owning pointer); the
// re-export must preserve type identity so a `WeakRef<RegionNode>` cache
// slot stays sizeof(RegionNode*)-compatible with the substrate primary
// and the null-discipline / guarded-deref is one and the same type.
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::WeakRef<int>,
    ::crucible::safety::WeakRef<int>>,
    "fixy::wrap::WeakRef must alias safety::WeakRef.");

// Bits identity — exercised by FIXY-U-096o Graph.h migration.  Bits<E, Inv...>
// is a non-Graded structural newtype (typed bit-field over a scoped enum);
// the re-export must preserve type identity so Graph::Node::flags stays
// layout-compatible with the substrate primary.  Sentinel uses a local
// scoped-enum tag — type identity holds across any Bits<E> instantiation.
namespace bits_sentinel_detail {
    enum class WrapBitsSentinelE : std::uint8_t { kA = 1, kB = 2 };
}
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::Bits<bits_sentinel_detail::WrapBitsSentinelE>,
    ::crucible::safety::Bits<bits_sentinel_detail::WrapBitsSentinelE>>,
    "fixy::wrap::Bits must alias safety::Bits.");

// SwissTableBuffer identity — exercised by FIXY-U-096n ExprPool.h migration.
// SwissTableBuffer<Slot> is a non-Graded structural RAII owner of an
// open-addressing aligned coupled allocation (ctrl bytes + slot array).
// Move-only by design (deleted copy ctor with reason string); the
// re-export must preserve type identity so the ExprPool::backing_
// member field stays layout-compatible with the substrate primary.
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::SwissTableBuffer<void*>,
    ::crucible::safety::SwissTableBuffer<void*>>,
    "fixy::wrap::SwissTableBuffer must alias safety::SwissTableBuffer.");

// add_sat_checked / sub_sat_checked / mul_sat_checked identity — exercised
// by FIXY-U-096b (Saturate.h migration).  Function-template identity is
// witnessed by decltype-equality on the function-pointer type: a using-
// declaration does not introduce a new function entity, so `&fwrap::fn<T>`
// and `&safety::fn<T>` are the same expression spelled two ways, with
// identical decltype.  Same pattern as the predicate-lambda sentinels
// above.  (Pointer-equality `==` triggers `-Werror=tautological-compare`
// because GCC folds both sides to the same address — the type-identity
// rail dodges that and proves what we need.)
static_assert(std::is_same_v<
    decltype(&::crucible::fixy::wrap::add_sat_checked<std::uint64_t>),
    decltype(&::crucible::safety::add_sat_checked<std::uint64_t>)>,
    "fixy::wrap::add_sat_checked must alias safety::add_sat_checked.");
static_assert(std::is_same_v<
    decltype(&::crucible::fixy::wrap::sub_sat_checked<std::uint64_t>),
    decltype(&::crucible::safety::sub_sat_checked<std::uint64_t>)>,
    "fixy::wrap::sub_sat_checked must alias safety::sub_sat_checked.");
static_assert(std::is_same_v<
    decltype(&::crucible::fixy::wrap::mul_sat_checked<std::uint64_t>),
    decltype(&::crucible::safety::mul_sat_checked<std::uint64_t>)>,
    "fixy::wrap::mul_sat_checked must alias safety::mul_sat_checked.");

// ─── Per-tier sub-namespace aliases (13 cells, FIXY-U-093 follow-up) ──
//
// One witness per sub-namespace — pick a representative alias from each
// and prove it equals the substrate equivalent.  Drift here would mean
// a typo in the `namespace X = ::crucible::safety::X;` aliases above.
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::hot_path::Hot<int>,
    ::crucible::safety::hot_path::Hot<int>>,
    "fixy::wrap::hot_path::Hot must alias safety::hot_path::Hot.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::det_safe::Pure<int>,
    ::crucible::safety::det_safe::Pure<int>>,
    "fixy::wrap::det_safe::Pure must alias safety::det_safe::Pure.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::numerical_tier::Bitexact<int>,
    ::crucible::safety::numerical_tier::Bitexact<int>>,
    "fixy::wrap::numerical_tier::Bitexact must alias substrate.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::vendor::Nv<int>,
    ::crucible::safety::vendor::Nv<int>>,
    "fixy::wrap::vendor::Nv must alias safety::vendor::Nv.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::residency_heat::Hot<int>,
    ::crucible::safety::residency_heat::Hot<int>>,
    "fixy::wrap::residency_heat::Hot must alias substrate.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::cipher_tier::Hot<int>,
    ::crucible::safety::cipher_tier::Hot<int>>,
    "fixy::wrap::cipher_tier::Hot must alias safety::cipher_tier::Hot.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::alloc_class::Arena<int>,
    ::crucible::safety::alloc_class::Arena<int>>,
    "fixy::wrap::alloc_class::Arena must alias safety::alloc_class::Arena.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::wait::SpinPause<int>,
    ::crucible::safety::wait::SpinPause<int>>,
    "fixy::wrap::wait::SpinPause must alias safety::wait::SpinPause.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::mem_order::AcqRel<int>,
    ::crucible::safety::mem_order::AcqRel<int>>,
    "fixy::wrap::mem_order::AcqRel must alias safety::mem_order::AcqRel.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::progress::Terminating<int>,
    ::crucible::safety::progress::Terminating<int>>,
    "fixy::wrap::progress::Terminating must alias substrate.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::consistency::Strong<int>,
    ::crucible::safety::consistency::Strong<int>>,
    "fixy::wrap::consistency::Strong must alias safety::consistency::Strong.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::opaque_lifetime::PerFleet<int>,
    ::crucible::safety::opaque_lifetime::PerFleet<int>>,
    "fixy::wrap::opaque_lifetime::PerFleet must alias substrate.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::crash::NoThrow<int>,
    ::crucible::safety::crash::NoThrow<int>>,
    "fixy::wrap::crash::NoThrow must alias safety::crash::NoThrow.");

}  // namespace crucible::fixy::wrap::self_test

namespace crucible::fixy::wrap {
// ── runtime_smoke_test — FIXY-U-103 discipline ───────────────────
// Exercise the round-2 surface at runtime to defeat clangd's "static-
// asserts only" optimization.  Touches one Graded canonical, one
// off-tree, one structural, and one ct:: primitive to prove the
// using-declarations are address-resolvable at link time.
inline void runtime_smoke_test() noexcept {
    // One Tier-S canonical: HotPath has a public T-arg ctor.
    HotPath<HotPathTier_v::Hot, int> hot{42};
    (void) hot;

    // One off-tree: Budgeted has a public default ctor.
    Budgeted<int> b{};
    (void) b;

    // ct:: primitive — exercises the constant-time sub-namespace.
    unsigned const masked = ct::select<unsigned>(1u, 0xAAu, 0x55u);
    (void) masked;

    // ScopedView / OwnedRegion are friend-gated; the using-decl reach
    // is anchored by self_test:: static_asserts above without ctor
    // invocation here (their mint factories need protocol-specific
    // customization that's out of scope for a smoke test).

    // Per-tier sub-namespace round-trip — construct via fixy alias path.
    residency_heat::Hot<int>  hot_int{1};
    residency_heat::Warm<int> warm_int{2};
    residency_heat::Cold<int> cold_int{3};
    (void) hot_int; (void) warm_int; (void) cold_int;
}
}  // namespace crucible::fixy::wrap
