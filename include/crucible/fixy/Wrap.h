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
//   • 7 structural (deliberately not Graded) wrappers
//     (Pinned, NonMovable, ScopedView, OwnedRegion,
//      NotInherited, FinalBy, ct::* primitives)
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
#include <crucible/safety/AllocClass.h>        // canonical Tier-S
#include <crucible/safety/Borrowed.h>          // non-owning lifetime-tagged view
#include <crucible/safety/IsBorrowedRef.h>     // IsBorrowedRef concept gate
#include <crucible/safety/Budgeted.h>          // off-tree (Space axis)
#include <crucible/safety/Saturated.h>         // {value, was_clamped} carrier
#include <crucible/safety/CipherTier.h>        // canonical Tier-S
#include <crucible/safety/Consistency.h>       // off-tree (Version axis)
#include <crucible/safety/ConstantTime.h>      // structural (ct::* primitives)
#include <crucible/safety/Crash.h>             // off-tree (Effect axis)
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
#include <crucible/safety/ResidencyHeat.h>     // canonical Tier-S
#include <crucible/safety/ScopedView.h>        // structural (lifetime borrow)
#include <crucible/safety/SealedRefined.h>
#include <crucible/safety/Secret.h>
#include <crucible/safety/Stale.h>
#include <crucible/safety/Tagged.h>
#include <crucible/safety/TimeOrdered.h>
#include <crucible/safety/Vendor.h>            // canonical Tier-S
#include <crucible/safety/Wait.h>              // canonical Tier-S

#include <cstdint>       // FIXY-U-020 sentinel uses std::uint64_t
#include <type_traits>   // FIXY-U-020 sentinel uses std::is_same_v

namespace crucible::fixy::wrap {

// ─── Graded-backed wrappers (11) ─────────────────────────────────

// Linear<T> — move-only consume-once.
using ::crucible::safety::Linear;
using ::crucible::safety::mint_linear;
using ::crucible::safety::drop;

// Refined<Pred, T> — predicate-checked at construction.
using ::crucible::safety::Refined;
// Named refinement aliases — load-bearing per CLAUDE.md §XVI.
using ::crucible::safety::NonNull;
using ::crucible::safety::Positive;
using ::crucible::safety::NonNegative;
using ::crucible::safety::PowerOfTwo;
// Refined composition with Linear (both orderings).
using ::crucible::safety::LinearRefined;
using ::crucible::safety::RefinedLinear;
// Common stateless predicates — usable as Refined NTTP.
using ::crucible::safety::positive;
using ::crucible::safety::non_negative;
using ::crucible::safety::non_zero;
using ::crucible::safety::non_null;
using ::crucible::safety::power_of_two;
using ::crucible::safety::non_empty;
// Parameterised predicate templates.
using ::crucible::safety::Aligned;
using ::crucible::safety::InRange;
using ::crucible::safety::BoundedAbove;
using ::crucible::safety::LengthGe;
using ::crucible::safety::aligned;
using ::crucible::safety::in_range;
using ::crucible::safety::bounded_above;
using ::crucible::safety::length_ge;
// Cross-predicate implication trait — used by SessionPayloadSubsort.
using ::crucible::safety::predicate_implies;
using ::crucible::safety::implies_v;

// SealedRefined<Pred, T> — Refined with no into() extractor.
using ::crucible::safety::SealedRefined;

// Tagged<T, Source> — phantom-tag provenance / trust marker.
using ::crucible::safety::Tagged;

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

// Saturated<T> — saturating-arithmetic result carrying {value,
// was_clamped} discriminant.  safety/Saturated.h primary symbol —
// surfaced here per FIXY-U-093 (MerkleDag.h compute_storage_nbytes).
using ::crucible::safety::Saturated;

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

// NotInherited<T> (concept) / FinalBy<T> (CRTP base) — structural
// non-extensibility.  NotInherited is a `concept`, not a class, so it
// is brought in as a name only (no per-tier alias possible).
using ::crucible::safety::NotInherited;
using ::crucible::safety::assert_not_inherited;
using ::crucible::safety::FinalBy;

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

}  // namespace crucible::fixy::wrap

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
