#pragma once

// ── crucible::safety::diag — RowHash recursive fmix64 fold ─────────
//
// FOUND-I02.  Computes a 64-bit `RowHash` (Types.h, FOUND-I01) over a
// type T's *row signature* — the Met(X) effect-row content folded into
// a single content-addressable identifier.
//
// Foundation for the FOUND-I federation cache key:
//
//   KernelCacheKey { ContentHash, RowHash } ── FOUND-I01
//                                  └──── computed by THIS header (FOUND-I02)
//
// Two computations with byte-identical `content_hash` but different
// `row_hash` represent the *same computation under different effect
// regimes* — they MUST cache to distinct slots (CRUCIBLE.md §10,
// FORGE.md §23.2).  A Pure-row kernel is trivially federatable; an
// IO-row kernel is not; sharing a slot between them silently breaks
// the federation contract.
//
// ── Design contract ─────────────────────────────────────────────────
//
// 1. **Permutation-invariance.**  `Row<A, B>` and `Row<B, A>` share
//    semantics under the Subrow relation — the type system treats
//    them as equivalent (set semantics over effect atoms).  The hash
//    therefore canonicalizes the effect pack via sort-on-underlying-
//    value before folding.  Without this, two semantically-equivalent
//    cache keys would address different slots and federation would
//    fragment.
//
// 2. **Cardinality discrimination.**  `Row<A>` and `Row<A, B>` have
//    different semantics — the second carries a strictly stronger
//    capability claim (`A ⊑ A∪B`).  The fold visits every element, so
//    cardinality automatically participates in the hash.
//
// 3. **Bare-type baseline.**  A bare type (e.g. `int`, `float`) has
//    no row contribution; its `row_hash_contribution<T>::value == 0`.
//    This is the cache-default sentinel meaning "no row" and matches
//    the NSDMI default of `KernelCacheKey::row_hash`.
//
// 4. **`EmptyRow` is NOT zero.**  `Row<>` is a *real* row with zero
//    effects — semantically distinct from "no row" (a bare `int`).  We
//    seed the EmptyRow hash with the FNV-1a offset basis (mixed by
//    fmix64 once) so its bit pattern is fixed and non-zero.  Without
//    this distinction, `Computation<EmptyRow, int>` and a bare `int`
//    would alias, which is wrong: the former carries a row-typed
//    Met(X) carrier; the latter is just a payload.
//
// 5. **Federation V1 stability.**  `Effect` is an enum class with
//    fixed underlying values (Capabilities.h).  Hashing the underlying
//    `uint8_t` values yields the same RowHash on every compiler / TU
//    that includes the same `effects/Capabilities.h` revision.  This
//    is the V1 federation guarantee.  A breaking change to the
//    `Effect` enum (renumbering, deletion) is a CDAG_VERSION bump and
//    a wire-format break, exactly as documented in the Family-A
//    persistent-hash taxonomy in Types.h.
//
// 6. **Recursive fold over wrapper stack.**  The trait
//    `row_hash_contribution<T>` is the open extension point.  Wrappers
//    that carry row-relevant state supply specializations that fold
//    *their* contribution with the inner type's row hash via
//    `combine_ids` — the same Boost-style combiner used by
//    StableName.h.
//
// ── Currently shipped specializations (FOUND-I02 + I02-AUDIT) ──────
//
// As of this header revision, the row-bearing core and every wrapper
// in the canonical wrapper-nesting order are wired:
//
//   row_hash_contribution<effects::Row<Es...>>
//       — sort-fold over Effect underlying values; cardinality-
//         seeded.  See spec block below.
//
//   row_hash_contribution<effects::Computation<R, T>>
//       — combine_ids(R-hash, T-hash); payload-blind for bare T,
//         row-discriminating, nested-non-collapsing.  See spec
//         block below.
//
//   row_hash_contribution<W<...Inner...>>
//       — combine_ids(wrapper-stable-tag, Inner-hash) for:
//         HotPath / DetSafe / NumericalTier / Vendor / ResidencyHeat /
//         CipherTier / AllocClass / Wait / MemOrder / Progress / Stale /
//         Tagged / Refined / Secret / Linear.
//
// CLAUDE.md §XVI / FOUND-I03 documents this scope at user-facing
// resolution.
//
// ── References ──────────────────────────────────────────────────────
//
//   28_04_2026_effects.md §7.3 + §8     — design rationale
//   safety/diag/StableName.h            — FNV-1a + fmix64 primitives,
//                                         combine_ids
//   effects/EffectRow.h                 — Row<Es...> shape
//   effects/Capabilities.h              — Effect enum + values
//   Types.h                             — RowHash strong type
//   MerkleDag.h §9 KernelCache          — open-addressing cache that
//                                         consumes row-keyed entries
//
// FOUND-I02 — RowHash recursive fmix64 fold over wrapper stack.

#include <crucible/Expr.h>                       // detail::fmix64
#include <crucible/Platform.h>
#include <crucible/Types.h>                      // RowHash strong type
#include <crucible/effects/Capabilities.h>       // Effect enum
#include <crucible/effects/EffectRow.h>          // Row<Es...>
#include <crucible/safety/diag/StableName.h>     // FNV1A_OFFSET_BASIS, combine_ids

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

// Forward-declare effects::Computation rather than pull in
// `effects/Computation.h`'s full graded substrate (which would
// transitively drag Graded.h, the lattice family, and stringly-typed
// reflection paths into every TU that includes RowHashFold).  A
// specialization of row_hash_contribution<effects::Computation<R, T>>
// only needs to deduce its template parameters; the class definition
// is irrelevant.  Any TU that USES Computation<...> as the argument
// type must include effects/Computation.h itself — that's the standard
// IWYU rule, not a constraint we add here.
namespace crucible::effects {
template <typename R, typename T> class Computation;
}  // namespace crucible::effects

namespace crucible::algebra::lattices {
enum class AllocClassTag : std::uint8_t;
enum class CipherTierTag : std::uint8_t;
enum class Consistency : std::uint8_t;       // A3-003
enum class CrashClass : std::uint8_t;        // A3-003
enum class DetSafeTier : std::uint8_t;
enum class HotPathTier : std::uint8_t;
enum class Lifetime : std::uint8_t;          // A3-003
enum class MemOrderTag : std::uint8_t;
enum class ProgressClass : std::uint8_t;
enum class ResidencyHeatTag : std::uint8_t;
enum class Tolerance : std::uint8_t;
enum class VendorBackend : std::uint8_t;
enum class WaitStrategy : std::uint8_t;
enum class Witness : std::uint8_t;            // FIXY-V-053
enum class JoinPolicy : std::uint8_t;         // FIXY-V-078
// FIXY-V-088 — 11 FP-mode sub-axis enums.  Forward-declared here so
// the V-090 row_hash_contribution specializations below can dispatch
// on the NTTP type without pulling FpModeLattice.h.
enum class FpRounding         : std::uint8_t;
enum class FpFtz              : std::uint8_t;
enum class FpContract         : std::uint8_t;
enum class FpTrapMask         : std::uint8_t;
enum class FpDenormalInput    : std::uint8_t;
enum class FpNanPolicy        : std::uint8_t;
enum class FpInfPolicy        : std::uint8_t;
enum class FpComplexLayout    : std::uint8_t;
enum class FpLibmPolicy       : std::uint8_t;
enum class FpReassociate      : std::uint8_t;
enum class FpConstantRounding : std::uint8_t;
enum class HwInstruction       : std::uint8_t;  // FIXY-V-251 (wrapper V-254)
enum class BarrierStrength     : std::uint8_t;  // FIXY-V-252 (wrapper V-255)
enum class SimdIsa             : std::uint8_t;  // FIXY-V-250 (wrapper V-256)
enum class MemoryScope         : std::uint8_t;  // FIXY-V-265 (wrapper V-267)
enum class ClockSource         : std::uint8_t;  // FIXY-V-184 (wrapper V-185)
enum class SchedulerPolicy     : std::uint8_t;  // FIXY-V-183 (wrapper V-186)
enum class SuspendBehavior     : std::uint8_t;  // FIXY-V-181 (wrapper V-188)
}  // namespace crucible::algebra::lattices

namespace crucible::safety {
template <algebra::lattices::AllocClassTag Tag, typename T> class AllocClass;
template <algebra::lattices::CipherTierTag Tier, typename T> class CipherTier;
template <algebra::lattices::DetSafeTier Tier, typename T> class DetSafe;
template <algebra::lattices::HotPathTier Tier, typename T> class HotPath;
template <algebra::lattices::HwInstruction Tier, typename T> class Hw;
template <algebra::lattices::BarrierStrength Tier, typename T> class BarrierGuarded;
template <algebra::lattices::SimdIsa W, typename T> class SimdWidthPinned;
template <algebra::lattices::MemoryScope S, typename T> class ScopedFence;
template <algebra::lattices::ClockSource Source, typename T> class ClockSource;
template <algebra::lattices::SchedulerPolicy Policy, typename T,
          std::uint64_t RuntimeNs, std::uint64_t DeadlineNs, std::uint64_t PeriodNs>
class SchedClass;
template <algebra::lattices::SuspendBehavior Behavior, typename T> class SuspendBehavior;
template <algebra::lattices::MemOrderTag Tag, typename T> class MemOrder;
template <algebra::lattices::ProgressClass Class, typename T> class Progress;
template <algebra::lattices::ResidencyHeatTag Tier, typename T> class ResidencyHeat;
template <algebra::lattices::Tolerance Tier, typename T> class NumericalTier;
template <algebra::lattices::VendorBackend Backend, typename T> class Vendor;
template <algebra::lattices::WaitStrategy Strategy, typename T> class Wait;
template <typename T> class Linear;
template <auto Pred, typename T> class Refined;
template <typename T> class Secret;
template <typename T> class Stale;
template <typename T, typename Tag> class Tagged;
// ── A3-003 — 11 Graded-bearing wrappers ────────────────────────────
template <auto Pred, typename T> class SealedRefined;
template <typename T, std::size_t N, typename Tag> class TimeOrdered;
template <typename T, typename Cmp> class Monotonic;
template <typename T, template <typename...> class Storage> class AppendOnly;
template <algebra::lattices::Consistency Level, typename T> class Consistency;
template <algebra::lattices::Lifetime Scope, typename T> class OpaqueLifetime;
template <algebra::lattices::CrashClass Class, typename T> class Crash;
template <typename T> class Budgeted;
template <typename T> class EpochVersioned;
template <typename T> class NumaPlacement;
template <typename T> class RecipeSpec;
// ── FIXY-V-054 — Witness (Comonad over WitnessLattice chain) ───────
template <algebra::lattices::Witness Tier, typename T> class Witness;
// ── FIXY-V-079 — JoinPolicy (Comonad over JoinPolicyLattice chain) ─
template <algebra::lattices::JoinPolicy Tier, typename T> class JoinPolicy;
// ── FIXY-V-090 — FpModePinned<auto Mode, T> (Absolute, regime-1) ───
// The 11 type aliases (FpRoundingPinned, FpFtzPinned, ...) instantiate
// this template with different NTTP enum types — the partial
// specializations of row_hash_contribution below dispatch on the
// NTTP type to apply the right WRAPPER_FP_*_TAG salt.
// Forward-decl is unconstrained; the real definition in safety/FpMode.h
// carries `requires detail::fp_mode_traits::IsFpAxisMode<decltype(Mode)>`
// — that constraint isn't reachable from this header, so the forward-decl
// is intentionally constraint-free (C++ permits a more-constrained
// definition to follow a less-constrained forward-decl).
template <auto Mode, typename T>
class FpModePinned;
}  // namespace crucible::safety

namespace crucible::safety::diag {

// ═════════════════════════════════════════════════════════════════════
// ── Internal fold helpers ──────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail {

inline constexpr std::uint64_t WRAPPER_HOTPATH_TAG        = 0x0100'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_DETSAFE_TAG        = 0x0200'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_NUMERICAL_TIER_TAG = 0x0300'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_VENDOR_TAG         = 0x0400'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_RESIDENCY_HEAT_TAG = 0x0500'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_CIPHER_TIER_TAG    = 0x0600'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_ALLOC_CLASS_TAG    = 0x0700'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_WAIT_TAG           = 0x0800'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_MEM_ORDER_TAG      = 0x0900'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_PROGRESS_TAG       = 0x0A00'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_STALE_TAG          = 0x0B00'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_TAGGED_TAG         = 0x0C00'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_REFINED_TAG        = 0x0D00'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_SECRET_TAG         = 0x0E00'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_LINEAR_TAG         = 0x0F00'0000'0000'0000ULL;
// ── A3-002: ResourceTag + ConcurrentRow row-hash salts ─────────────
//
// ResourceTag and ConcurrentRow live in the OS-effect-row neighborhood
// but are distinct row-bearing kinds (per-tag value-carrying + per-
// carrier additive fold).  Without their own high-byte salt every
// `resource::*<N>` instantiation collides with the primary-template
// zero contribution, and every `ConcurrentRow<...>` carrier silently
// folds to the federation cache slot zero — both witnessed by fixy-
// A3-002.  Salts 0x10 and 0x11 keep the resource family disjoint from
// the existing 15 canonical-wrapper salts above.  Specializations live
// in effects/Resources.h and effects/Concurrent.h (per A1-018 "spec
// next to declaration") and refer back to these constants.
inline constexpr std::uint64_t WRAPPER_RESOURCE_TAG_TAG   = 0x1000'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_CONCURRENT_ROW_TAG = 0x1100'0000'0000'0000ULL;

// ── A3-003: 11 Graded-bearing wrappers from DimensionTraits.h ──────
//
// Post-GAPS-028 the DimensionTraits.h `wrapper_dimension<W>` registry
// added 11 NEW Graded-bearing wrappers beyond the canonical 15 above
// (TimeOrdered, EpochVersioned, Budgeted, Consistency, RecipeSpec,
// NumaPlacement, OpaqueLifetime, Crash, SealedRefined, Monotonic,
// AppendOnly).  Without their own high-byte salts every instantiation
// collides with the primary-template zero contribution; CLAUDE.md
// §XVI's "16-of-16 wrappers covered" claim becomes false once
// production code starts wrapping with these — federation cache slot
// collision exactly as A3-002 witnessed for ResourceTag.  Salts 0x12-
// 0x1C keep the eleven disjoint from each other AND from the existing
// 15 canonical-wrapper salts above AND from the resource family
// (0x10-0x11).  Specializations live in this header alongside the
// canonical 15 (centralized convention for safety::* wrappers).
inline constexpr std::uint64_t WRAPPER_SEALED_REFINED_TAG   = 0x1200'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_TIME_ORDERED_TAG     = 0x1300'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_MONOTONIC_TAG        = 0x1400'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_APPEND_ONLY_TAG      = 0x1500'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_CONSISTENCY_TAG      = 0x1600'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_OPAQUE_LIFETIME_TAG  = 0x1700'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_CRASH_TAG            = 0x1800'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_BUDGETED_TAG         = 0x1900'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_EPOCH_VERSIONED_TAG  = 0x1A00'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_NUMA_PLACEMENT_TAG   = 0x1B00'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_RECIPE_SPEC_TAG      = 0x1C00'0000'0000'0000ULL;

// ── FIXY-V-001 / V-002: Fn aggregator + fixy::fn facade row-hash salts ─
//
// The 19-axis `safety::fn::Fn<Type, ...>` aggregator AND the higher-level
// `fixy::fn<Type, Grants...>` facade are BOTH row-bearing kinds — every
// capability claim attached to a binding (UsageMode, EffectRow,
// SecLevel, Source, Trust, Repr, Cost, Precision, Space, Overflow,
// Mutation, Reentrancy, Size, Version, Staleness, plus Refinement /
// Protocol / Lifetime / Cost type-valued axes) must contribute to the
// federation cache key, otherwise two capability-divergent bindings
// over the same payload Type collapse to row_hash == 0 and route to
// the SAME (ContentHash, RowHash) cache slot.
//
// This is the bug Agent 4 verified at /tmp/audit_test.cpp on the
// patched GCC 16: every `fixy::fn<Type, Stance1...>` and
// `fixy::fn<Type, Stance2...>` with identical payload `Type` produced
// `row_hash_contribution_v == 0` regardless of stance divergence on
// Effect/Usage/Security/Vendor/Recipe/Hot/Wait/MemOrder/Allocator/
// CipherTier/Numa axes. Spec §7(b) federation discharge ("cross-vendor
// numerics correctness is enforced before the binary leaves the
// publishing organization") was silently unsound.
//
// Salt allocations (per FOUND-I02 high-byte discipline):
//   0x1D — Fn aggregator (substrate, safety::fn::Fn)
//   0x1E — fixy::fn facade (binds Type+Grants pack, resolves via
//          ::safety_fn_t for permutation-invariant cache slot)
//
// Specializations live in:
//   safety/Fn.h           — `safety::fn::Fn<Type, ...>` 19-axis fold
//                           (per A1-018 "spec next to declaration")
//   fixy/Fn.h             — `fixy::fn<Type, Grants...>` facade fold
//                           that delegates to ::safety_fn_t hash so
//                           Grant-pack permutations resolve to the
//                           SAME cache slot (find_grant_t<D, Grants...>
//                           is permutation-invariant under
//                           UniqueEngagementPerAxis).
//
// Salts 0x1D / 0x1E are disjoint from each other AND from the existing
// 0x01-0x1C canonical-wrapper salts AND from the resource family
// (0x10-0x11) — same separation discipline as A3-002 / A3-003.
inline constexpr std::uint64_t WRAPPER_SAFETY_FN_TAG        = 0x1D00'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_FIXY_FN_TAG          = 0x1E00'0000'0000'0000ULL;

// ── FIXY-V-054 / V-055: Witness (Comonad over WitnessLattice chain) ─
//
// `safety::Witness<algebra::lattices::Witness Tier, T>` is a regime-1
// Graded carrier on the Observability axis (CLAUDE.md §XVI canonical
// wrapper-nesting order, FX dim 11 slot — previously unoccupied).  The
// 4-tier chain UNWITNESSED ⊏ TYPE_CHECKED ⊏ TEST_PASSED ⊏
// FORMALLY_VERIFIED encodes epistemic confidence; downstream consumers
// that demand a minimum tier (e.g. `mimic::nv::Kernel<FORMALLY_VERIFIED,
// CompiledKernel>` per V-176) MUST cache to a slot disjoint from the
// same payload at TYPE_CHECKED — otherwise a verified kernel and a
// merely-type-checked kernel collide at the federation key.  Salt 0x1F
// keeps the Witness specialization disjoint from the existing 0x01-0x1E
// salts AND from the resource family (0x10-0x11) AND from the A3-003
// dimension-traits family (0x12-0x1C) AND from the Fn aggregator family
// (0x1D-0x1E).  Low-byte folds the Tier enumerator the same way
// Consistency / OpaqueLifetime / Crash / Wait / MemOrder / Progress do.
inline constexpr std::uint64_t WRAPPER_WITNESS_TAG          = 0x1F00'0000'0000'0000ULL;

// ── FIXY-V-079: JoinPolicy (Comonad over JoinPolicyLattice chain) ───
//
// `safety::JoinPolicy<algebra::lattices::JoinPolicy Tier, T>` is a
// regime-1 Graded carrier on the Synchronization axis (CLAUDE.md §XVI
// canonical wrapper-nesting order, dim 20 — shared with Wait + MemOrder
// as concurrency-discipline annotations).  The 6-tier chain FORGET ⊏
// DETACH ⊏ ABANDON ⊏ CANCEL ⊏ WAIT_DEADLINE ⊏ JOIN_ALL encodes the
// parent's structural-concurrency engagement with its spawned children;
// downstream consumers that demand a minimum tier (e.g. a region body
// that needs `mint_spawn` to actually have joined every worker)
// MUST cache to a slot disjoint from the same payload at a weaker
// tier — otherwise a `JoinPolicy<JOIN_ALL, T>` result and a
// `JoinPolicy<FORGET, T>` result collide at the federation key.
// Salt 0x20 keeps the JoinPolicy specialization disjoint from the
// existing 0x01-0x1F salts (Witness occupies 0x1F).  Low-byte folds
// the Tier enumerator the same way Witness / Wait / MemOrder /
// Progress / Crash / Consistency do.
inline constexpr std::uint64_t WRAPPER_JOIN_POLICY_TAG      = 0x2000'0000'0000'0000ULL;

// FIXY-V-090 — 11 FP-mode sub-axis carriers + 1 composite mint slot.
// Each sub-axis lives at its own salt so a downstream consumer
// reasoning about Forge's RecipeSelect phase can distinguish a value
// computed under FpRounding::RoundToNearestEven from the same value
// under RoundToZero (the two are NOT byte-equivalent under any FP
// operation that crosses a rounding boundary).  Salts 0x21..0x2B map
// 1-to-1 with the V-088 sub-axis ordinal; the 11-axis FpModeComposite
// type carries NO salt of its own — its row_hash composes through the
// per-axis specializations automatically (see safety/FpMode.h's
// FpModeComposite alias).  The Agent-11 HW-axis wrappers continue the
// sequence: 0x2C = Hw (V-254), 0x2D = BarrierGuarded (V-255),
// 0x2E = SimdWidthPinned (V-256).
inline constexpr std::uint64_t WRAPPER_FP_ROUNDING_TAG          = 0x2100'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_FP_FTZ_TAG               = 0x2200'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_FP_CONTRACT_TAG          = 0x2300'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_FP_TRAP_MASK_TAG         = 0x2400'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_FP_DENORMAL_INPUT_TAG    = 0x2500'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_FP_NAN_POLICY_TAG        = 0x2600'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_FP_INF_POLICY_TAG        = 0x2700'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_FP_COMPLEX_LAYOUT_TAG    = 0x2800'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_FP_LIBM_POLICY_TAG       = 0x2900'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_FP_REASSOCIATE_TAG       = 0x2A00'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_FP_CONSTANT_ROUNDING_TAG = 0x2B00'0000'0000'0000ULL;
// FIXY-V-254 — Hw<HwInstruction Tier, T> federation-cache discriminator.
inline constexpr std::uint64_t WRAPPER_HW_INSTRUCTION_TAG       = 0x2C00'0000'0000'0000ULL;
// FIXY-V-255 — BarrierGuarded<BarrierStrength Tier, T> discriminator.
inline constexpr std::uint64_t WRAPPER_BARRIER_STRENGTH_TAG     = 0x2D00'0000'0000'0000ULL;
// FIXY-V-256 — SimdWidthPinned<SimdIsa W, T> discriminator.
inline constexpr std::uint64_t WRAPPER_SIMD_ISA_TAG            = 0x2E00'0000'0000'0000ULL;
// FIXY-V-267 — ScopedFence<MemoryScope S, T> discriminator.
inline constexpr std::uint64_t WRAPPER_MEMORY_SCOPE_TAG       = 0x2F00'0000'0000'0000ULL;
// FIXY-V-185 — ClockSource<ClockSource Source, T> discriminator.  Salt
// 0x30 is the next free high-byte after 0x2F (MemoryScope); the low byte
// folds the ClockSource enumerator (0..8) so distinct sources land in
// disjoint slots.  The composite ClockSourceLattice (V-184) carries NO
// salt of its own — the federation-cache contribution lives HERE, on the
// wrapper, exactly as V-184 deferred it.
inline constexpr std::uint64_t WRAPPER_CLOCK_SOURCE_TAG       = 0x3000'0000'0000'0000ULL;
// FIXY-V-186 — SchedClass<SchedulerPolicy Policy, T, R, D, P> discriminator.
// Salt 0x31 is the next free high-byte after 0x30 (ClockSource); the low
// byte folds the SchedulerPolicy enumerator (0..5) and the SCHED_DEADLINE
// budget NTTPs are mixed in so distinct CBS budgets occupy distinct slots
// (non-DEADLINE policies carry a zero budget → the mix vanishes).
inline constexpr std::uint64_t WRAPPER_SCHED_CLASS_TAG       = 0x3100'0000'0000'0000ULL;
// FIXY-V-187 — CpuPinned<AffinityMask Mask, PinningPosture Posture, T>
// discriminator.  Salt 0x32 is the next free high-byte after 0x31
// (SchedClass).  UNLIKE every other wrapper, CpuPinned's `Mask` is a CLASS
// NTTP (the 256-bit AffinityMask), not an enum — using it in a
// forward-declared specialization here would force this widely-included
// header to #include the full algebra/lattices/AffinityLattice.h.  To keep
// RowHashFold lean, the `row_hash_contribution<CpuPinned<...>>`
// specialization lives in safety/CpuPinned.h (which already has AffinityMask
// complete); only the collision-checked salt constant is centralized here.
inline constexpr std::uint64_t WRAPPER_CPU_PINNED_TAG       = 0x3200'0000'0000'0000ULL;
// FIXY-V-188 — SuspendBehavior<SuspendBehavior Behavior, T> discriminator.
// Salt 0x33 is the next free high-byte after 0x32 (CpuPinned); the low byte
// folds the SuspendBehavior enumerator (0..2).  This is the row_hash the
// V-181 SuspendBehaviorLattice deferred — on the WRAPPER, never the At<>.
inline constexpr std::uint64_t WRAPPER_SUSPEND_BEHAVIOR_TAG = 0x3300'0000'0000'0000ULL;

// Bubble-sort a fixed-size std::array<uint64_t, N> in place at
// consteval.  N is bounded by `effects::effect_count` (≤ 64 by
// axiom — EffectRowLattice's defensive cap, FOUND-H01-AUDIT-2), so
// O(N²) is fine and cheaper than introducing <algorithm> dependence
// for a bounded compile-time problem.
template <std::size_t N>
[[nodiscard]] consteval std::array<std::uint64_t, N>
sorted_uints(std::array<std::uint64_t, N> xs) noexcept {
    for (std::size_t i = 0; i < N; ++i) {
        for (std::size_t j = i + 1; j < N; ++j) {
            if (xs[j] < xs[i]) {
                std::uint64_t const tmp = xs[i];
                xs[i] = xs[j];
                xs[j] = tmp;
            }
        }
    }
    return xs;
}

// fmix64-fold over an already-canonical (sorted) array.  The seed
// MUST encode cardinality (caller's responsibility) — otherwise an
// effect with underlying value 0 (currently `Effect::Alloc`) silently
// collides with `EmptyRow`: `fmix64(seed ^ 0) == fmix64(seed)`.
template <std::size_t N>
[[nodiscard]] consteval std::uint64_t
fmix64_fold(std::array<std::uint64_t, N> const& xs,
            std::uint64_t seed) noexcept {
    std::uint64_t h = seed;
    for (std::size_t i = 0; i < N; ++i) {
        h = ::crucible::detail::fmix64(h ^ xs[i]);
    }
    return h;
}

// Count the unique values in a sorted array.  O(N) walk: every
// transition where xs[i] != xs[i-1] adds one unique.  Returns 0 for
// the empty array and 1 for any singleton.  Used by the Row<Es...>
// specialization to seed cardinality_seed() with the SET-cardinality
// (the number of distinct Effect atoms) rather than the raw pack
// size — otherwise `Row<IO, IO>` and `Row<IO>` would differ in seed
// even though the body's dedup-fold renders the rest identical.
template <std::size_t N>
[[nodiscard]] consteval std::size_t
unique_count_sorted(std::array<std::uint64_t, N> const& xs) noexcept {
    if constexpr (N == 0) {
        return 0;
    } else {
        std::size_t count = 1;
        for (std::size_t i = 1; i < N; ++i) {
            if (xs[i] != xs[i - 1]) ++count;
        }
        return count;
    }
}

// fmix64-fold over the UNIQUE values of a sorted array.  Only the
// first occurrence at each value contributes; later adjacent
// duplicates are skipped.  Together with unique_count_sorted() this
// turns the Row hash into a set-semantic function — two rows with
// the same effect set produce identical hashes regardless of
// declaration order OR atom multiplicity.
//
// The seed MUST encode UNIQUE-cardinality (caller's responsibility);
// see unique_count_sorted() for the rationale.
template <std::size_t N>
[[nodiscard]] consteval std::uint64_t
fmix64_fold_unique_sorted(std::array<std::uint64_t, N> const& xs,
                          std::uint64_t seed) noexcept {
    if constexpr (N == 0) {
        return seed;
    } else {
        std::uint64_t h = ::crucible::detail::fmix64(seed ^ xs[0]);
        for (std::size_t i = 1; i < N; ++i) {
            if (xs[i] != xs[i - 1]) {
                h = ::crucible::detail::fmix64(h ^ xs[i]);
            }
        }
        return h;
    }
}

// Cardinality-mixing seed factory.  Every row hash starts here:
// the seed FNV1A_OFFSET_BASIS is XOR'd with N (the row's
// cardinality) and then mixed.  This guarantees Row<X> with N
// effects and Row<Y> with M ≠ N effects cannot collide regardless of
// XOR-fold coincidences inside the body.
[[nodiscard]] consteval std::uint64_t
cardinality_seed(std::uint64_t cardinality) noexcept {
    return ::crucible::detail::fmix64(FNV1A_OFFSET_BASIS ^ cardinality);
}

// EmptyRow hash — the seed produced for cardinality 0.  Pulled out
// so the constant is shared between the EmptyRow specialization and
// the self-test, and so any future audit can compare against it.
inline constexpr std::uint64_t EMPTY_ROW_HASH = cardinality_seed(0);

}  // namespace detail

// ═════════════════════════════════════════════════════════════════════
// ── row_hash_contribution<T> — open extension point ────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Primary template: any T contributes 0 to its row hash.  Specialized
// per row-bearing type kind:
//   • effects::Row<Es...>   (THIS file — sort-fold over Effect values)
//   • effects::Computation<R, T>  (THIS file — delegates to R)
//   • HotPath<T, Tier> / DetSafe<T, Tier> / ...  (FOUND-I05/06/07)
//
// Specializations supplied OUTSIDE this file MUST live in the same
// `crucible::safety::diag` namespace and follow the recursive
// composition discipline:
//
//   row_hash_contribution<W<T, ...attrs...>>::value =
//       combine_ids(<W's tag bits>, row_hash_contribution<T>::value)
//
// where `combine_ids` is the Boost-style combiner from StableName.h.
// `combine_ids` is order-sensitive — wrapper position in the stack
// matters (HotPath<DetSafe<T>> ≢ DetSafe<HotPath<T>>).  This is the
// canonical wrapper-nesting order documented in FOUND-I03.

template <typename T>
struct row_hash_contribution {
    static constexpr std::uint64_t value = 0;
};

template <typename T>
inline constexpr std::uint64_t row_hash_contribution_v =
    row_hash_contribution<T>::value;

// ═════════════════════════════════════════════════════════════════════
// ── Row<Es...> specialization — set-semantic sort+dedup fold ───────
// ═════════════════════════════════════════════════════════════════════
//
// **Set semantics, not multiset.**  `Row<Es...>` denotes a set of
// Effect atoms — `row_union_t`, `row_intersection_t`, and
// `row_difference_t` are inherently set-shaped (see EffectRow.h §92-
// 104).  The hash function honors that: it is invariant under both
// (a) permutation of the pack and (b) duplicate-atom drift, so
// `Row<IO, IO> ≡ Row<IO>` and `Row<Bg, IO, Bg> ≡ Row<IO, Bg>` by
// hash, matching set-equality.
//
// Without dedup, a future caller that produces `Row<IO, IO>` (e.g.
// by composing two policies that each declare IO before METX-2's
// type-level canonicalization lands at #474) would fragment the
// federation cache into N+1 slots for the same semantic row — a
// silent correctness AND scale defect.  Set-semantic hashing closes
// this regardless of when canonicalization arrives.
//
// Federation-pin stability: every pinned row in the self-test block
// below has all-distinct atoms, so the dedup pass is a no-op for the
// pinned cases — the published hashes do not move when this
// specialization gains dedup.

template <effects::Effect... Es>
struct row_hash_contribution<effects::Row<Es...>> {
    static constexpr std::uint64_t value = []() consteval -> std::uint64_t {
        constexpr std::size_t N = sizeof...(Es);
        if constexpr (N == 0) {
            // EmptyRow path: cardinality_seed(0).  Preserved bit-
            // exactly so detail::EMPTY_ROW_HASH stays the published
            // constant.
            return detail::cardinality_seed(0);
        } else {
            std::array<std::uint64_t, N> const raw_vals{
                static_cast<std::uint64_t>(
                    static_cast<std::underlying_type_t<effects::Effect>>(Es))...
            };
            auto const sorted = detail::sorted_uints(raw_vals);
            // Cardinality is mixed FIRST as the SET-cardinality (the
            // number of distinct atoms), NOT the raw pack size — see
            // detail::unique_count_sorted for the duplicate-drift
            // rationale.  Mixing the cardinality first also guards
            // against the Effect::Alloc = 0 / EmptyRow collision
            // (fmix64(seed ^ 0) == fmix64(seed)).
            std::size_t const unique_n = detail::unique_count_sorted(sorted);
            std::uint64_t const seed = detail::cardinality_seed(unique_n);
            return detail::fmix64_fold_unique_sorted(sorted, seed);
        }
    }();
};

// ═════════════════════════════════════════════════════════════════════
// ── Computation<R, T> specialization — the primary Met(X) consumer ─
// ═════════════════════════════════════════════════════════════════════
//
// `effects::Computation<R, T>` is the canonical row-typed Met(X)
// carrier (Computation.h, FOUND-B, Tang-Lindley POPL 2026).  It is
// THE primary consumer of row hashes: the federation cache key for
// any compiled kernel that lifts its result through a row carrier
// must reflect *both* the row R and the payload T's row contribution.
//
// Two design constraints any specialization here must satisfy:
//
//   (1) **Distinct from bare R.**  `row_hash_contribution_v<Row<A>>`
//       must not equal `row_hash_contribution_v<Computation<Row<A>,
//       int>>`.  A bare row is metadata; a Computation is a value.
//       The cache must distinguish them or two semantically-different
//       calls would collide at the same slot.
//
//   (2) **Payload-blind for bare T.**  `Computation<R, int>` and
//       `Computation<R, double>` MUST hash identically — the cache
//       key is row-shape, not payload-shape (payload identity is
//       carried by `ContentHash`, the other half of KernelCacheKey).
//
//   (3) **Non-collapsing for row-bearing T.**  When T is itself row-
//       bearing (e.g. nested `Computation<Row<IO>, int>` inside a
//       `Computation<EmptyRow, ...>`), the inner row participates in
//       the outer hash so that nested vs. flat carriers cannot alias.
//       This protects the cache against a pathological "monad-in-
//       monad" stash that semantically differs from the flattened
//       form even before `then` collapses it.
//
// `combine_ids(row_hash_v<R>, row_hash_v<T>)` satisfies all three:
//
//   - combine_ids is order-sensitive and Boost-style golden-ratio
//     mixed; for any non-trivial X, `combine_ids(X, 0) ≠ X` —
//     guarantees (1) when T is a bare type contributing 0.
//   - For T1, T2 with `row_hash_contribution_v<T1> ==
//     row_hash_contribution_v<T2> == 0` (bare types), the result is
//     identical — guarantees (2).
//   - When T is row-bearing, its non-zero contribution flows in —
//     guarantees (3).
//
// Order-sensitivity (combine_ids(R, T) ≠ combine_ids(T, R)) is
// deliberate.  It pins canonical metadata-before-payload contribution
// WITHIN the Computation carrier — R (the row) folds first, T (the
// payload) folds second.
//
// fixy-A3-016 clarification — "outer/inner" terminology differs at the
// two scales an audit reader bridges between this header and CLAUDE.md
// §XVI's wrapper-nesting order.  They are NOT in conflict:
//
//   * WRAPPER-STACK level (§XVI, FOUND-I03):
//       `HotPath<Tier, Computation<R, T>>` — HotPath is the OUTER
//       wrapper, Computation is the INNER carrier.  §XVI says
//       Computation is the INNERMOST member of every effect stack.
//       The wrapper specializations below (`HotPath<Tier, Inner>`, …)
//       fold `combine_ids(<wrapper tag>, row_hash_v<Inner>)` —
//       wrapper-tag first, inner contribution second.
//
//   * COMPUTATION-INTERNAL level (this specialization):
//       Within a single `Computation<R, T>` instantiation, R is the
//       row metadata and T is the payload.  Calling R "outer" and T
//       "inner" describes their position in the combine_ids fold INSIDE
//       Computation — NOT their position in the wrapper stack (the
//       whole Computation IS inside any wrapper that wraps it).
//
// Both order properties fold deterministically and orthogonally; a
// reader who confuses them would expect the wrong row_hash for
// `Computation<R, HotPath<Tier, int>>` vs
// `HotPath<Tier, Computation<R, int>>` (which produce different
// hashes — pinned by the wrapper-stack × Computation interleave test
// in test_row_hash_fold.cpp).

template <typename R, typename T>
struct row_hash_contribution<effects::Computation<R, T>> {
    static constexpr std::uint64_t value =
        detail::combine_ids(
            row_hash_contribution_v<R>,
            row_hash_contribution_v<T>);
};

// ═════════════════════════════════════════════════════════════════════
// ── Canonical safety-wrapper stack specializations ─────────────────
// ═════════════════════════════════════════════════════════════════════
//
// These specializations keep the row hash sensitive to wrapper order.
// Wrapper attributes participate where they are part of the type-level
// semantics: enum tiers fold their underlying value, Tagged folds the
// source tag type, and Refined folds the predicate object's type.  The
// wrapper tag itself lives in the high byte so enum values cannot
// collide across wrapper families.

template <algebra::lattices::HotPathTier Tier, typename Inner>
struct row_hash_contribution<safety::HotPath<Tier, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_HOTPATH_TAG | static_cast<std::uint64_t>(Tier),
        row_hash_contribution_v<Inner>);
};

template <algebra::lattices::DetSafeTier Tier, typename Inner>
struct row_hash_contribution<safety::DetSafe<Tier, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_DETSAFE_TAG | static_cast<std::uint64_t>(Tier),
        row_hash_contribution_v<Inner>);
};

template <algebra::lattices::Tolerance Tier, typename Inner>
struct row_hash_contribution<safety::NumericalTier<Tier, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_NUMERICAL_TIER_TAG | static_cast<std::uint64_t>(Tier),
        row_hash_contribution_v<Inner>);
};

template <algebra::lattices::VendorBackend Backend, typename Inner>
struct row_hash_contribution<safety::Vendor<Backend, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_VENDOR_TAG | static_cast<std::uint64_t>(Backend),
        row_hash_contribution_v<Inner>);
};

// FIXY-V-254 — Hw sits between Vendor (which backend) and ResidencyHeat
// (where the value lives) in the §XVI canonical wrapper-nesting order.
template <algebra::lattices::HwInstruction Tier, typename Inner>
struct row_hash_contribution<safety::Hw<Tier, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_HW_INSTRUCTION_TAG | static_cast<std::uint64_t>(Tier),
        row_hash_contribution_v<Inner>);
};

// FIXY-V-255 — BarrierGuarded is a Repr-neighborhood wrapper peer to Hw;
// the publication-fence tier discriminates the federation-cache slot.
template <algebra::lattices::BarrierStrength Tier, typename Inner>
struct row_hash_contribution<safety::BarrierGuarded<Tier, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_BARRIER_STRENGTH_TAG | static_cast<std::uint64_t>(Tier),
        row_hash_contribution_v<Inner>);
};

// FIXY-V-256 — SimdWidthPinned is a Repr-neighborhood wrapper (Tier-L
// Lattice) peer to Vendor; the pinned SIMD ISA discriminates the slot.
template <algebra::lattices::SimdIsa W, typename Inner>
struct row_hash_contribution<safety::SimdWidthPinned<W, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_SIMD_ISA_TAG | static_cast<std::uint64_t>(W),
        row_hash_contribution_v<Inner>);
};

// FIXY-V-267 — ScopedFence is a Repr-neighborhood wrapper (Tier-L Lattice)
// peer to SimdWidthPinned; the pinned memory-visibility scope discriminates
// the slot.  MemoryScope's underlying value packs the trunk into the high
// nibble, so distinct-trunk scopes (e.g. Cta=0x11 vs Inner=0x20) and the
// shared sentinels (Thread=0x00, System=0xFF) all land in disjoint salt
// low-bytes.
template <algebra::lattices::MemoryScope S, typename Inner>
struct row_hash_contribution<safety::ScopedFence<S, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_MEMORY_SCOPE_TAG | static_cast<std::uint64_t>(S),
        row_hash_contribution_v<Inner>);
};

// FIXY-V-185 — ClockSource is a Representation-neighborhood wrapper
// (Tier-L Lattice) sitting between the Repr cluster (Vendor / SimdWidthPinned
// / ScopedFence) and ResidencyHeat; the pinned clock source discriminates
// the federation-cache slot.  This is the row_hash the V-184 composite
// lattice deferred — the contribution lives on the WRAPPER, never the
// lattice At<>.  The low byte folds the ClockSource enumerator (0..9 after
// FIXY-V-201 appended PtpHwClock at ordinal 9).
template <algebra::lattices::ClockSource Source, typename Inner>
struct row_hash_contribution<safety::ClockSource<Source, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_CLOCK_SOURCE_TAG | static_cast<std::uint64_t>(Source),
        row_hash_contribution_v<Inner>);
};

// FIXY-V-186 — SchedClass is a Synchronization-neighborhood wrapper (peer
// to Wait / MemOrder); the pinned scheduler policy discriminates the slot,
// and the SCHED_DEADLINE budget NTTPs are folded so two DEADLINE tasks
// with different (runtime, deadline, period) never alias.  This is the
// row_hash the V-183 SchedulerPolicyLattice deferred — on the WRAPPER,
// never the lattice At<>.
template <algebra::lattices::SchedulerPolicy Policy, typename Inner,
          std::uint64_t RuntimeNs, std::uint64_t DeadlineNs, std::uint64_t PeriodNs>
struct row_hash_contribution<
    safety::SchedClass<Policy, Inner, RuntimeNs, DeadlineNs, PeriodNs>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::combine_ids(
            detail::WRAPPER_SCHED_CLASS_TAG | static_cast<std::uint64_t>(Policy),
            RuntimeNs ^ (DeadlineNs << 1) ^ (PeriodNs << 2)),
        row_hash_contribution_v<Inner>);
};

// FIXY-V-188 — SuspendBehavior is a Synchronization-neighborhood witness
// peer to ClockSource / SchedClass; the pinned pause-on-suspend behavior
// discriminates the slot.  This is the row_hash V-181 deferred.
template <algebra::lattices::SuspendBehavior Behavior, typename Inner>
struct row_hash_contribution<safety::SuspendBehavior<Behavior, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_SUSPEND_BEHAVIOR_TAG | static_cast<std::uint64_t>(Behavior),
        row_hash_contribution_v<Inner>);
};

template <algebra::lattices::ResidencyHeatTag Tier, typename Inner>
struct row_hash_contribution<safety::ResidencyHeat<Tier, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_RESIDENCY_HEAT_TAG | static_cast<std::uint64_t>(Tier),
        row_hash_contribution_v<Inner>);
};

template <algebra::lattices::CipherTierTag Tier, typename Inner>
struct row_hash_contribution<safety::CipherTier<Tier, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_CIPHER_TIER_TAG | static_cast<std::uint64_t>(Tier),
        row_hash_contribution_v<Inner>);
};

template <algebra::lattices::AllocClassTag Tag, typename Inner>
struct row_hash_contribution<safety::AllocClass<Tag, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_ALLOC_CLASS_TAG | static_cast<std::uint64_t>(Tag),
        row_hash_contribution_v<Inner>);
};

template <algebra::lattices::WaitStrategy Strategy, typename Inner>
struct row_hash_contribution<safety::Wait<Strategy, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_WAIT_TAG | static_cast<std::uint64_t>(Strategy),
        row_hash_contribution_v<Inner>);
};

template <algebra::lattices::MemOrderTag Tag, typename Inner>
struct row_hash_contribution<safety::MemOrder<Tag, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_MEM_ORDER_TAG | static_cast<std::uint64_t>(Tag),
        row_hash_contribution_v<Inner>);
};

template <algebra::lattices::ProgressClass Class, typename Inner>
struct row_hash_contribution<safety::Progress<Class, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_PROGRESS_TAG | static_cast<std::uint64_t>(Class),
        row_hash_contribution_v<Inner>);
};

template <typename Inner>
struct row_hash_contribution<safety::Stale<Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_STALE_TAG,
        row_hash_contribution_v<Inner>);
};

template <typename Inner, typename Source>
struct row_hash_contribution<safety::Tagged<Inner, Source>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::combine_ids(
            detail::WRAPPER_TAGGED_TAG,
            stable_type_id<Source>),
        row_hash_contribution_v<Inner>);
};

template <auto Pred, typename Inner>
struct row_hash_contribution<safety::Refined<Pred, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::combine_ids(
            detail::WRAPPER_REFINED_TAG,
            stable_type_id<std::remove_cvref_t<decltype(Pred)>>),
        row_hash_contribution_v<Inner>);
};

template <typename Inner>
struct row_hash_contribution<safety::Secret<Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_SECRET_TAG,
        row_hash_contribution_v<Inner>);
};

template <typename Inner>
struct row_hash_contribution<safety::Linear<Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_LINEAR_TAG,
        row_hash_contribution_v<Inner>);
};

// ═════════════════════════════════════════════════════════════════════
// ── A3-003 specializations — 11 Graded-bearing safety wrappers ─────
// ═════════════════════════════════════════════════════════════════════
//
// Post-GAPS-028 the DimensionTraits.h `wrapper_dimension<W>` registry
// (safety/DimensionTraits.h:407-509) grew from the canonical 15 above
// to 26 entries.  Without per-wrapper row_hash_contribution every new
// instantiation collapses to the primary-template zero and federation-
// cache-keys collide — same defect class as A3-002 closed for the
// ResourceTag family.  The 11 specializations below restore the
// invariant "every Graded-bearing wrapper folds its own bits into
// row_hash" for the entire DimensionTraits.h registry.

// SealedRefined<Pred, T> — same shape as Refined<Pred, T>: the
// predicate's type folds into the hash so two refinements over the
// same Inner with different predicates produce distinct slots.
template <auto Pred, typename Inner>
struct row_hash_contribution<safety::SealedRefined<Pred, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::combine_ids(
            detail::WRAPPER_SEALED_REFINED_TAG,
            stable_type_id<std::remove_cvref_t<decltype(Pred)>>),
        row_hash_contribution_v<Inner>);
};

// TimeOrdered<T, N, Tag> — bucket count N and lane tag both
// participate.  Two TimeOrdered carriers with the same Inner but
// distinct N or Tag are semantically different (different ring sizes,
// different threading discipline) — they MUST cache separately.
// N folds in directly: std::size_t is already uint64_t on every
// supported Crucible platform (x86_64 / aarch64, CLAUDE.md §XIV), so
// the cast is useless under -Werror=useless-cast.
template <typename Inner, std::size_t N, typename Tag>
struct row_hash_contribution<safety::TimeOrdered<Inner, N, Tag>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::combine_ids(
            detail::combine_ids(
                detail::WRAPPER_TIME_ORDERED_TAG,
                N),
            stable_type_id<Tag>),
        row_hash_contribution_v<Inner>);
};

// Monotonic<T, Cmp> — the comparator type folds.  Two monotonic
// counters with the same Inner but distinct Cmp (e.g. std::less vs
// std::greater for ascending vs descending sequences) are different
// row-bearing values.
template <typename Inner, typename Cmp>
struct row_hash_contribution<safety::Monotonic<Inner, Cmp>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::combine_ids(
            detail::WRAPPER_MONOTONIC_TAG,
            stable_type_id<Cmp>),
        row_hash_contribution_v<Inner>);
};

// AppendOnly<T, Storage> — Storage is a template-template parameter
// and cannot directly participate in stable_type_id (which takes a
// concrete type).  The wrapper-tag distinguishes the wrapper family;
// callers that swap Storage classes will produce identical row hashes,
// which matches the contract that AppendOnly is policy-bearing only
// at the value-level (the storage choice is an implementation detail,
// not a row-relevant property).
template <typename Inner, template <typename...> class Storage>
struct row_hash_contribution<safety::AppendOnly<Inner, Storage>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_APPEND_ONLY_TAG,
        row_hash_contribution_v<Inner>);
};

// Consistency<Level, T> — enum-encoded tier in the salt's low byte.
template <algebra::lattices::Consistency Level, typename Inner>
struct row_hash_contribution<safety::Consistency<Level, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_CONSISTENCY_TAG | static_cast<std::uint64_t>(Level),
        row_hash_contribution_v<Inner>);
};

// OpaqueLifetime<Scope, T> — enum-encoded scope in the salt's low
// byte.  Distinct scopes (PER_FLEET / PER_NODE / PER_PROCESS / ...)
// route to disjoint cache slots.
template <algebra::lattices::Lifetime Scope, typename Inner>
struct row_hash_contribution<safety::OpaqueLifetime<Scope, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_OPAQUE_LIFETIME_TAG | static_cast<std::uint64_t>(Scope),
        row_hash_contribution_v<Inner>);
};

// Crash<Class, T> — enum-encoded BSYZ22 crash class.  NoThrow /
// ErrorReturn / Throw / Abort tier different recovery contracts and
// therefore distinct row-bearing values.
template <algebra::lattices::CrashClass Class, typename Inner>
struct row_hash_contribution<safety::Crash<Class, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_CRASH_TAG | static_cast<std::uint64_t>(Class),
        row_hash_contribution_v<Inner>);
};

// Budgeted<T> / EpochVersioned<T> / NumaPlacement<T> / RecipeSpec<T>
// — single-template-arg wrappers.  No internal attributes to fold;
// just the wrapper-tag salt over Inner's contribution.
template <typename Inner>
struct row_hash_contribution<safety::Budgeted<Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_BUDGETED_TAG,
        row_hash_contribution_v<Inner>);
};

template <typename Inner>
struct row_hash_contribution<safety::EpochVersioned<Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_EPOCH_VERSIONED_TAG,
        row_hash_contribution_v<Inner>);
};

template <typename Inner>
struct row_hash_contribution<safety::NumaPlacement<Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_NUMA_PLACEMENT_TAG,
        row_hash_contribution_v<Inner>);
};

template <typename Inner>
struct row_hash_contribution<safety::RecipeSpec<Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_RECIPE_SPEC_TAG,
        row_hash_contribution_v<Inner>);
};

// ═════════════════════════════════════════════════════════════════════
// ── FIXY-V-055 — Witness<Tier, T> specialization (Observability) ───
// ═════════════════════════════════════════════════════════════════════
//
// `safety::Witness<Tier, T>` is the Observability-axis canonical
// inhabitant (CLAUDE.md §XVI, FX dim 11).  The 4-tier WitnessLattice
// (UNWITNESSED ⊏ TYPE_CHECKED ⊏ TEST_PASSED ⊏ FORMALLY_VERIFIED)
// encodes epistemic confidence at the type level: at production V-176
// `mimic::nv::Kernel<FORMALLY_VERIFIED, CompiledKernel>` declares a
// formally-verified cert return; the same payload at TYPE_CHECKED is
// the merely-discipline-witnessed default.  Two such carriers MUST hash
// to disjoint federation cache slots — a verified kernel and a type-
// checked kernel are not interchangeable, regardless of byte-identical
// payload.  Low-byte encodes the Tier enumerator the same way
// Consistency / OpaqueLifetime / Crash / Progress / Wait / MemOrder
// fold their lattice tag.
template <algebra::lattices::Witness Tier, typename Inner>
struct row_hash_contribution<safety::Witness<Tier, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_WITNESS_TAG | static_cast<std::uint64_t>(Tier),
        row_hash_contribution_v<Inner>);
};

// ── FIXY-V-079 — JoinPolicy<Tier, T> federation hash ────────────────
//
// `safety::JoinPolicy<algebra::lattices::JoinPolicy Tier, T>` pins the
// parent's structural-concurrency engagement (FORGET ⊏ DETACH ⊏
// ABANDON ⊏ CANCEL ⊏ WAIT_DEADLINE ⊏ JOIN_ALL) into the type.  A
// consumer that demands a minimum tier MUST cache to a slot disjoint
// from the same payload at a weaker tier — otherwise a region that
// joined every child and a region that abandoned them silently
// collide.  Salt 0x20 keeps the JoinPolicy specialization disjoint
// from Witness (0x1F) and from every other wrapper salt; low-byte
// folds the Tier enumerator the same way Witness / Wait / MemOrder
// / Progress / Crash / Consistency do.
template <algebra::lattices::JoinPolicy Tier, typename Inner>
struct row_hash_contribution<safety::JoinPolicy<Tier, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_JOIN_POLICY_TAG | static_cast<std::uint64_t>(Tier),
        row_hash_contribution_v<Inner>);
};

// ── FIXY-V-090 — 11 FP-mode sub-axis wrappers ──────────────────────
//
// Each `safety::FpModePinned<Mode, T>` (under per-axis aliases
// FpRoundingPinned / FpFtzPinned / ...) pins a single FP evaluation
// policy.  The 11 specializations below dispatch on the NTTP type
// (enum type of Mode) — distinct partial specializations because
// each takes a Mode parameter of a distinct enum type.  Salts are
// allocated from RowHashFold's WRAPPER_FP_*_TAG constants (0x21..0x2B).
//
// Composition rule: a stacked FpModeComposite<R, F, C, ..., Cr, T>
// resolves to 11 nested FpModePinned<Mode, ...> layers; the row_hash
// folds through each layer's specialization automatically because
// each layer's Inner is the next layer down.  No composite-specific
// specialization is needed — the 11 per-axis salts compose.
template <algebra::lattices::FpRounding Mode, typename Inner>
struct row_hash_contribution<safety::FpModePinned<Mode, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_FP_ROUNDING_TAG | static_cast<std::uint64_t>(Mode),
        row_hash_contribution_v<Inner>);
};

template <algebra::lattices::FpFtz Mode, typename Inner>
struct row_hash_contribution<safety::FpModePinned<Mode, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_FP_FTZ_TAG | static_cast<std::uint64_t>(Mode),
        row_hash_contribution_v<Inner>);
};

template <algebra::lattices::FpContract Mode, typename Inner>
struct row_hash_contribution<safety::FpModePinned<Mode, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_FP_CONTRACT_TAG | static_cast<std::uint64_t>(Mode),
        row_hash_contribution_v<Inner>);
};

template <algebra::lattices::FpTrapMask Mode, typename Inner>
struct row_hash_contribution<safety::FpModePinned<Mode, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_FP_TRAP_MASK_TAG | static_cast<std::uint64_t>(Mode),
        row_hash_contribution_v<Inner>);
};

template <algebra::lattices::FpDenormalInput Mode, typename Inner>
struct row_hash_contribution<safety::FpModePinned<Mode, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_FP_DENORMAL_INPUT_TAG | static_cast<std::uint64_t>(Mode),
        row_hash_contribution_v<Inner>);
};

template <algebra::lattices::FpNanPolicy Mode, typename Inner>
struct row_hash_contribution<safety::FpModePinned<Mode, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_FP_NAN_POLICY_TAG | static_cast<std::uint64_t>(Mode),
        row_hash_contribution_v<Inner>);
};

template <algebra::lattices::FpInfPolicy Mode, typename Inner>
struct row_hash_contribution<safety::FpModePinned<Mode, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_FP_INF_POLICY_TAG | static_cast<std::uint64_t>(Mode),
        row_hash_contribution_v<Inner>);
};

template <algebra::lattices::FpComplexLayout Mode, typename Inner>
struct row_hash_contribution<safety::FpModePinned<Mode, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_FP_COMPLEX_LAYOUT_TAG | static_cast<std::uint64_t>(Mode),
        row_hash_contribution_v<Inner>);
};

template <algebra::lattices::FpLibmPolicy Mode, typename Inner>
struct row_hash_contribution<safety::FpModePinned<Mode, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_FP_LIBM_POLICY_TAG | static_cast<std::uint64_t>(Mode),
        row_hash_contribution_v<Inner>);
};

template <algebra::lattices::FpReassociate Mode, typename Inner>
struct row_hash_contribution<safety::FpModePinned<Mode, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_FP_REASSOCIATE_TAG | static_cast<std::uint64_t>(Mode),
        row_hash_contribution_v<Inner>);
};

template <algebra::lattices::FpConstantRounding Mode, typename Inner>
struct row_hash_contribution<safety::FpModePinned<Mode, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_FP_CONSTANT_ROUNDING_TAG | static_cast<std::uint64_t>(Mode),
        row_hash_contribution_v<Inner>);
};

// ═════════════════════════════════════════════════════════════════════
// ── Top-level entry points ─────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// `row_hash_of<T>()` — consteval function returning RowHash.  Use
// this when you need to capture the result into a non-template local.
//
// `row_hash_of_v<T>` — variable template form for static_assert.

template <typename T>
[[nodiscard]] consteval RowHash row_hash_of() noexcept {
    return RowHash{row_hash_contribution_v<T>};
}

template <typename T>
inline constexpr RowHash row_hash_of_v = row_hash_of<T>();

// ═════════════════════════════════════════════════════════════════════
// ── Self-test block — invariants asserted at header inclusion ──────
// ═════════════════════════════════════════════════════════════════════

namespace detail::row_hash_self_test {

using effects::Effect;
using effects::EmptyRow;
using effects::Row;

// ─── Bare types contribute 0 ───────────────────────────────────────

static_assert(row_hash_contribution_v<int>      == 0);
static_assert(row_hash_contribution_v<float>    == 0);
static_assert(row_hash_contribution_v<double>   == 0);
static_assert(row_hash_contribution_v<void>     == 0);
static_assert(row_hash_contribution_v<unsigned> == 0);

static_assert(row_hash_of_v<int>   == RowHash{0});
static_assert(row_hash_of_v<float> == RowHash{0});

// Bare-type RowHash is_zero() — flows through to KernelCacheKey
// behavior: a (ContentHash, RowHash{0}) is NOT a sentinel slot, just
// a row-defaulted entry.
static_assert(!row_hash_of_v<int>.is_sentinel());

// ─── Singleton rows produce non-zero, distinct hashes ──────────────

static_assert(row_hash_contribution_v<Row<Effect::Alloc>> != 0);
static_assert(row_hash_contribution_v<Row<Effect::IO>>    != 0);
static_assert(row_hash_contribution_v<Row<Effect::Block>> != 0);
static_assert(row_hash_contribution_v<Row<Effect::Bg>>    != 0);
static_assert(row_hash_contribution_v<Row<Effect::Init>>  != 0);
static_assert(row_hash_contribution_v<Row<Effect::Test>>  != 0);

static_assert(row_hash_contribution_v<Row<Effect::Alloc>>
           != row_hash_contribution_v<Row<Effect::IO>>);
static_assert(row_hash_contribution_v<Row<Effect::Alloc>>
           != row_hash_contribution_v<Row<Effect::Block>>);
static_assert(row_hash_contribution_v<Row<Effect::IO>>
           != row_hash_contribution_v<Row<Effect::Bg>>);

// ─── Permutation invariance — pair, triple, full sextuple ──────────

// 2-way: Row<A,B> ≡ Row<B,A>.
static_assert(row_hash_contribution_v<Row<Effect::Alloc, Effect::IO>>
           == row_hash_contribution_v<Row<Effect::IO, Effect::Alloc>>);

static_assert(row_hash_contribution_v<Row<Effect::Block, Effect::Bg>>
           == row_hash_contribution_v<Row<Effect::Bg, Effect::Block>>);

// 3-way: all 6 permutations of {Alloc, IO, Block} hash identically.
static_assert(
    row_hash_contribution_v<Row<Effect::Alloc, Effect::IO, Effect::Block>>
 == row_hash_contribution_v<Row<Effect::Alloc, Effect::Block, Effect::IO>>);
static_assert(
    row_hash_contribution_v<Row<Effect::Alloc, Effect::IO, Effect::Block>>
 == row_hash_contribution_v<Row<Effect::IO, Effect::Alloc, Effect::Block>>);
static_assert(
    row_hash_contribution_v<Row<Effect::Alloc, Effect::IO, Effect::Block>>
 == row_hash_contribution_v<Row<Effect::IO, Effect::Block, Effect::Alloc>>);
static_assert(
    row_hash_contribution_v<Row<Effect::Alloc, Effect::IO, Effect::Block>>
 == row_hash_contribution_v<Row<Effect::Block, Effect::Alloc, Effect::IO>>);
static_assert(
    row_hash_contribution_v<Row<Effect::Alloc, Effect::IO, Effect::Block>>
 == row_hash_contribution_v<Row<Effect::Block, Effect::IO, Effect::Alloc>>);

// 6-way (full universe): a permutation of all six atoms hashes the
// same as the canonical declaration order.
using FullRow_canonical =
    Row<Effect::Alloc, Effect::IO, Effect::Block,
        Effect::Bg,    Effect::Init, Effect::Test>;
using FullRow_reversed =
    Row<Effect::Test,  Effect::Init, Effect::Bg,
        Effect::Block, Effect::IO,   Effect::Alloc>;
using FullRow_shuffled =
    Row<Effect::Block, Effect::Alloc, Effect::Test,
        Effect::IO,    Effect::Init,  Effect::Bg>;

static_assert(row_hash_contribution_v<FullRow_canonical>
           == row_hash_contribution_v<FullRow_reversed>);
static_assert(row_hash_contribution_v<FullRow_canonical>
           == row_hash_contribution_v<FullRow_shuffled>);

// ─── Set-semantic dedup — Row is a set, not a multiset ─────────────
//
// Duplicate-atom drift cannot fragment the federation cache:
// `Row<X, X>` and `Row<X>` MUST hash identically.  This protects the
// cache against pre-canonicalization paths that produce non-
// canonical packs (METX-2 / #474 lands type-level canonicalization
// later; until then the hash is the only line of defense).
//
// fixy-H-19 regression anchor: removing dedup would re-introduce
// federation-cache slot fragmentation across semantically-equal
// rows, silently breaking the SET-semantic guarantee documented in
// EffectRow.h §92-104 for row_union/intersection/difference.

// Singletons: dedup is a no-op (already unique) — Row<X, X> ≡ Row<X>.
static_assert(row_hash_contribution_v<Row<Effect::Alloc, Effect::Alloc>>
           == row_hash_contribution_v<Row<Effect::Alloc>>);
static_assert(row_hash_contribution_v<Row<Effect::IO, Effect::IO>>
           == row_hash_contribution_v<Row<Effect::IO>>);
static_assert(row_hash_contribution_v<Row<Effect::Block, Effect::Block>>
           == row_hash_contribution_v<Row<Effect::Block>>);
static_assert(row_hash_contribution_v<Row<Effect::Bg, Effect::Bg>>
           == row_hash_contribution_v<Row<Effect::Bg>>);

// Triple-replicated atom collapses to singleton.
static_assert(row_hash_contribution_v<Row<Effect::IO, Effect::IO, Effect::IO>>
           == row_hash_contribution_v<Row<Effect::IO>>);

// Pair + leading or trailing duplicate collapses to the pair.
static_assert(row_hash_contribution_v<
                  Row<Effect::Alloc, Effect::Alloc, Effect::IO>>
           == row_hash_contribution_v<Row<Effect::Alloc, Effect::IO>>);
static_assert(row_hash_contribution_v<
                  Row<Effect::Alloc, Effect::IO, Effect::IO>>
           == row_hash_contribution_v<Row<Effect::Alloc, Effect::IO>>);

// Interleaved duplicates: order before dedup doesn't matter.
static_assert(row_hash_contribution_v<
                  Row<Effect::Bg, Effect::IO, Effect::Bg, Effect::IO>>
           == row_hash_contribution_v<Row<Effect::IO, Effect::Bg>>);
static_assert(row_hash_contribution_v<
                  Row<Effect::IO, Effect::Bg, Effect::Bg, Effect::IO>>
           == row_hash_contribution_v<Row<Effect::IO, Effect::Bg>>);

// Cardinality-seed dedup: a 4-pack with 2 unique atoms must seed
// with unique-count=2, NOT raw-count=4 — pins that the seed factory
// is fed unique_count_sorted, not the raw pack size.
static_assert(row_hash_contribution_v<
                  Row<Effect::Alloc, Effect::Alloc,
                      Effect::IO,    Effect::IO>>
           == row_hash_contribution_v<Row<Effect::Alloc, Effect::IO>>);

// ─── Cardinality discriminates ─────────────────────────────────────
//
// Adding an effect changes the hash.  This is essential — the row
// algebra has Row<A> ⊊ Row<A, B>, and the cache must reflect the
// strictly stronger capability claim.

static_assert(row_hash_contribution_v<Row<Effect::Alloc>>
           != row_hash_contribution_v<Row<Effect::Alloc, Effect::IO>>);

static_assert(row_hash_contribution_v<Row<Effect::Alloc, Effect::IO>>
           != row_hash_contribution_v<Row<Effect::Alloc, Effect::IO, Effect::Block>>);

static_assert(row_hash_contribution_v<Row<Effect::Alloc>>
           != row_hash_contribution_v<Row<Effect::IO>>);

// Disjoint pairs hash differently.
static_assert(row_hash_contribution_v<Row<Effect::Alloc, Effect::IO>>
           != row_hash_contribution_v<Row<Effect::Block, Effect::Bg>>);

// ─── EmptyRow is non-zero, distinct from bare-type 0 ───────────────

static_assert(row_hash_contribution_v<EmptyRow> != 0);
static_assert(row_hash_contribution_v<EmptyRow> == detail::EMPTY_ROW_HASH);

// EmptyRow ≢ bare-type 0 — the row carrier must always discriminate
// from a non-row T.
static_assert(row_hash_contribution_v<EmptyRow>
           != row_hash_contribution_v<int>);

// EmptyRow ≢ any singleton row — the empty effect set is not the
// same as a single-effect set.  REGRESSION ANCHOR: this enumeration
// triggered the audit catch that motivated the cardinality-seeded
// fold.  Effect::Alloc has underlying value 0, and the XOR-fold's
// `seed ^ 0 == seed` identity made `Row<Alloc>` alias `EmptyRow`
// before the cardinality_seed() fix.  Every singleton over the full
// Effect universe is now pinned distinct from EmptyRow.
static_assert(row_hash_contribution_v<EmptyRow>
           != row_hash_contribution_v<Row<Effect::Alloc>>);
static_assert(row_hash_contribution_v<EmptyRow>
           != row_hash_contribution_v<Row<Effect::IO>>);
static_assert(row_hash_contribution_v<EmptyRow>
           != row_hash_contribution_v<Row<Effect::Block>>);
static_assert(row_hash_contribution_v<EmptyRow>
           != row_hash_contribution_v<Row<Effect::Bg>>);
static_assert(row_hash_contribution_v<EmptyRow>
           != row_hash_contribution_v<Row<Effect::Init>>);
static_assert(row_hash_contribution_v<EmptyRow>
           != row_hash_contribution_v<Row<Effect::Test>>);

// All 15 distinct singleton-pair comparisons — every pair of
// singleton rows must hash to different values.  Exhaustive over
// Effect × Effect (modulo symmetry).  Catches any future Effect
// renumbering that creates a value-collision.

static_assert(row_hash_contribution_v<Row<Effect::Alloc>>
           != row_hash_contribution_v<Row<Effect::IO>>);
static_assert(row_hash_contribution_v<Row<Effect::Alloc>>
           != row_hash_contribution_v<Row<Effect::Block>>);
static_assert(row_hash_contribution_v<Row<Effect::Alloc>>
           != row_hash_contribution_v<Row<Effect::Bg>>);
static_assert(row_hash_contribution_v<Row<Effect::Alloc>>
           != row_hash_contribution_v<Row<Effect::Init>>);
static_assert(row_hash_contribution_v<Row<Effect::Alloc>>
           != row_hash_contribution_v<Row<Effect::Test>>);
static_assert(row_hash_contribution_v<Row<Effect::IO>>
           != row_hash_contribution_v<Row<Effect::Block>>);
static_assert(row_hash_contribution_v<Row<Effect::IO>>
           != row_hash_contribution_v<Row<Effect::Bg>>);
static_assert(row_hash_contribution_v<Row<Effect::IO>>
           != row_hash_contribution_v<Row<Effect::Init>>);
static_assert(row_hash_contribution_v<Row<Effect::IO>>
           != row_hash_contribution_v<Row<Effect::Test>>);
static_assert(row_hash_contribution_v<Row<Effect::Block>>
           != row_hash_contribution_v<Row<Effect::Bg>>);
static_assert(row_hash_contribution_v<Row<Effect::Block>>
           != row_hash_contribution_v<Row<Effect::Init>>);
static_assert(row_hash_contribution_v<Row<Effect::Block>>
           != row_hash_contribution_v<Row<Effect::Test>>);
static_assert(row_hash_contribution_v<Row<Effect::Bg>>
           != row_hash_contribution_v<Row<Effect::Init>>);
static_assert(row_hash_contribution_v<Row<Effect::Bg>>
           != row_hash_contribution_v<Row<Effect::Test>>);
static_assert(row_hash_contribution_v<Row<Effect::Init>>
           != row_hash_contribution_v<Row<Effect::Test>>);

// ─── row_hash_of_v wraps the raw u64 in a RowHash strongly typed ───

static_assert(std::is_same_v<decltype(row_hash_of_v<int>), const RowHash>);
static_assert(row_hash_of_v<EmptyRow>.raw() == detail::EMPTY_ROW_HASH);

// Round-trip: row_hash_of equals raw RowHash construction.
static_assert(row_hash_of_v<Row<Effect::Alloc>>
           == RowHash{row_hash_contribution_v<Row<Effect::Alloc>>});

// ─── No accidental sentinel collision ──────────────────────────────
//
// Real row hashes must not collide with the cache EMPTY-slot marker
// (RowHash::sentinel() == UINT64_MAX).  fmix64 distributes uniformly
// over the full 64-bit range; the probability of any specific value
// is ~2^-64 per row.  Spot-check the rows we actually use.

static_assert(row_hash_contribution_v<EmptyRow>
           != static_cast<std::uint64_t>(-1));
static_assert(row_hash_contribution_v<Row<Effect::Alloc>>
           != static_cast<std::uint64_t>(-1));
static_assert(row_hash_contribution_v<FullRow_canonical>
           != static_cast<std::uint64_t>(-1));

// Same sentinel discipline for every singleton — any future Effect
// renumbering that lands a row hash on UINT64_MAX would silently
// poison the cache (real row claims an EMPTY slot).  Cheap to check,
// catastrophic to miss.
static_assert(row_hash_contribution_v<Row<Effect::IO>>
           != static_cast<std::uint64_t>(-1));
static_assert(row_hash_contribution_v<Row<Effect::Block>>
           != static_cast<std::uint64_t>(-1));
static_assert(row_hash_contribution_v<Row<Effect::Bg>>
           != static_cast<std::uint64_t>(-1));
static_assert(row_hash_contribution_v<Row<Effect::Init>>
           != static_cast<std::uint64_t>(-1));
static_assert(row_hash_contribution_v<Row<Effect::Test>>
           != static_cast<std::uint64_t>(-1));

// ─── Computation<R, T> — payload-blind, row-discriminating ─────────
//
// AUDIT REGRESSION ANCHORS (FOUND-I02-AUDIT, 2026-04-30): closing the
// gap that motivated the audit pass — without these, `Computation`
// (the primary Met(X) consumer of row hashes) had NO specialization
// and fell through to the primary template's `value = 0`, aliasing
// every bare type and every other Computation instantiation.

// (a) Distinct from bare types.  A Computation<EmptyRow, int> is a
// row-typed carrier; a bare int has no row.  Cache must distinguish.
static_assert(row_hash_contribution_v<effects::Computation<EmptyRow, int>>
           != row_hash_contribution_v<int>);
static_assert(row_hash_contribution_v<effects::Computation<EmptyRow, int>> != 0);

// (b) Distinct from the bare row.  Combining the row with the (zero-
// contribution) payload via combine_ids changes the value; this is
// exactly what (combine_ids(X, 0) ≠ X) buys us — without it,
// Computation<EmptyRow, int> would alias EmptyRow and the cache could
// not tell "the carrier" from "the row metadata".
static_assert(row_hash_contribution_v<effects::Computation<EmptyRow, int>>
           != row_hash_contribution_v<EmptyRow>);
static_assert(
    row_hash_contribution_v<effects::Computation<Row<Effect::Alloc>, int>>
 != row_hash_contribution_v<Row<Effect::Alloc>>);
static_assert(
    row_hash_contribution_v<effects::Computation<Row<Effect::IO>, int>>
 != row_hash_contribution_v<Row<Effect::IO>>);

// (c) Payload-blind for bare T.  ContentHash carries payload identity;
// row_hash MUST be payload-blind so a kernel that returns int and one
// that returns double share row signatures (different cache slots
// thanks to ContentHash, same row).
static_assert(row_hash_contribution_v<effects::Computation<EmptyRow, int>>
           == row_hash_contribution_v<effects::Computation<EmptyRow, double>>);
static_assert(row_hash_contribution_v<effects::Computation<EmptyRow, int>>
           == row_hash_contribution_v<effects::Computation<EmptyRow, float>>);
static_assert(
    row_hash_contribution_v<effects::Computation<Row<Effect::Alloc>, int>>
 == row_hash_contribution_v<effects::Computation<Row<Effect::Alloc>, char>>);

// (d) Row-discriminating.  Same payload, different row → different
// hash.  Federation correctness depends on this: an Alloc-row kernel
// and an IO-row kernel that compute the same int must NOT share a
// cache slot.
static_assert(
    row_hash_contribution_v<effects::Computation<Row<Effect::Alloc>, int>>
 != row_hash_contribution_v<effects::Computation<Row<Effect::IO>, int>>);
static_assert(
    row_hash_contribution_v<effects::Computation<Row<Effect::Alloc>, int>>
 != row_hash_contribution_v<effects::Computation<EmptyRow, int>>);

// (e) Permutation invariance lifts through Computation — Row<A,B> and
// Row<B,A> hash identically inside the carrier.  Direct corollary of
// the inner Row<Es...> specialization, but pin it explicitly so a
// future combine_ids implementation change can't silently break it.
static_assert(
    row_hash_contribution_v<
        effects::Computation<Row<Effect::Alloc, Effect::IO>, int>>
 == row_hash_contribution_v<
        effects::Computation<Row<Effect::IO, Effect::Alloc>, int>>);

// (f) Cardinality discrimination lifts through Computation — Row<A>
// strictly less than Row<A,B>, so wrapping each in Computation cannot
// alias.  Direct corollary, pinned explicitly.
static_assert(
    row_hash_contribution_v<effects::Computation<Row<Effect::Alloc>, int>>
 != row_hash_contribution_v<
        effects::Computation<Row<Effect::Alloc, Effect::IO>, int>>);

// (g) Nested Computation — the inner row participates.  This is the
// "monad-in-monad" non-collapsing guarantee: a `Computation<EmptyRow,
// Computation<Row<IO>, int>>` carries a non-zero T contribution that
// MUST flow up into the outer hash, distinguishing it from the flat
// `Computation<EmptyRow, int>`.  This protects the cache against
// pathological nested stashes that semantically differ even before
// `then` flattens them.
static_assert(
    row_hash_contribution_v<
        effects::Computation<EmptyRow,
            effects::Computation<Row<Effect::IO>, int>>>
 != row_hash_contribution_v<effects::Computation<EmptyRow, int>>);
static_assert(
    row_hash_contribution_v<
        effects::Computation<EmptyRow,
            effects::Computation<Row<Effect::IO>, int>>>
 != row_hash_contribution_v<effects::Computation<Row<Effect::IO>, int>>);

// (h) Computation<R, T> never sentinel-collides on common rows.
static_assert(row_hash_contribution_v<effects::Computation<EmptyRow, int>>
           != static_cast<std::uint64_t>(-1));
static_assert(
    row_hash_contribution_v<effects::Computation<FullRow_canonical, int>>
 != static_cast<std::uint64_t>(-1));

// ─── Federation hash stability — the wire-format pin (FOUND-I04) ───
//
// PINNED CANONICAL ROW HASHES.  These hex literals are the bit-for-
// bit federation contract: every L1 / L2 / L3 cache entry whose key
// includes one of these row hashes encodes that exact 64-bit value
// in its serialized form.  Any change here — to the underlying
// Effect values (Capabilities.h), to fmix64 / FNV1A_OFFSET_BASIS
// (Expr.h / StableName.h), or to the cardinality_seed / fmix64_fold
// algorithm (this header) — invalidates every published cache entry
// across every fleet on the network.
//
// **DO NOT EDIT THESE LITERALS** unless you are deliberately
// performing the federation-cache wire-format-break ceremony
// documented in `effects/Capabilities.h` § "Major-version event
// procedure".  A drift WITHOUT that ceremony is silent corruption
// of every federation peer's cache.
//
// The comprehensive set covers: EmptyRow + every singleton + a
// representative pair + the full-Universe row.  Adding new pins
// for additional rows is fine; CHANGING existing pins is the
// wire-format break.

static_assert(row_hash_contribution_v<EmptyRow>
           == 0xEFD01F60BA992926ULL,
    "EmptyRow row_hash drifted — federation wire-format break.  "
    "See FOUND-I04 ceremony in Capabilities.h.");
static_assert(row_hash_contribution_v<Row<Effect::Alloc>>
           == 0x436DAF9EDCB565C3ULL,
    "Row<Alloc> row_hash drifted — federation wire-format break.");
static_assert(row_hash_contribution_v<Row<Effect::IO>>
           == 0x6FBFD0F707B63BECULL,
    "Row<IO> row_hash drifted — federation wire-format break.");
static_assert(row_hash_contribution_v<Row<Effect::Block>>
           == 0x3117F06B828C9247ULL,
    "Row<Block> row_hash drifted — federation wire-format break.");
static_assert(row_hash_contribution_v<Row<Effect::Bg>>
           == 0x008A519814C8FC81ULL,
    "Row<Bg> row_hash drifted — federation wire-format break.");
static_assert(row_hash_contribution_v<Row<Effect::Init>>
           == 0x9E23FC5AC81DA675ULL,
    "Row<Init> row_hash drifted — federation wire-format break.");
static_assert(row_hash_contribution_v<Row<Effect::Test>>
           == 0x26A9EB08E748D58FULL,
    "Row<Test> row_hash drifted — federation wire-format break.");
static_assert(row_hash_contribution_v<Row<Effect::Alloc, Effect::IO>>
           == 0x6CC046F52E6D7663ULL,
    "Row<Alloc, IO> row_hash drifted — federation wire-format break.");
static_assert(row_hash_contribution_v<Row<Effect::Alloc, Effect::IO,
                Effect::Block, Effect::Bg, Effect::Init, Effect::Test>>
           == 0x1C9D0E4F548FAAD6ULL,
    "Full-Universe row row_hash drifted — federation wire-format break.");

// ─── Computation<R, T> hash pins (FOUND-I04 + FOUND-I02-AUDIT) ─────
//
// The Computation<R, T> specialization (FOUND-I02-AUDIT) folds the
// row R via combine_ids with the payload T's row contribution.  Pin
// canonical values to detect drift in EITHER:
//   • the combine_ids algorithm (StableName.h),
//   • the Computation<R, T> specialization itself (this file), or
//   • the inner Row<Es...> contributions (already pinned above).
//
// Coverage:
//   (1) Empty-row + bare payload  — combine_ids(EMPTY_ROW_HASH, 0)
//   (2) Singleton row + bare payload — Bg-context kernel return value
//   (3) Pair-row + bare payload — Alloc+IO carrier
//   (4) Nested Computation — outer empty, inner has IO row.  Pins
//       that nested non-collapsing flows through combine_ids.
//
// Same federation-wire-format-break severity as the Row pins above.

static_assert(row_hash_contribution_v<effects::Computation<EmptyRow, int>>
           == 0x49A55BE1CFC23FB0ULL,
    "Computation<EmptyRow, int> row_hash drifted — federation wire-"
    "format break.  Either combine_ids or Computation<R, T> "
    "specialization changed.");
static_assert(
    row_hash_contribution_v<effects::Computation<Row<Effect::Bg>, int>>
 == 0x3ACE35615F0F9243ULL,
    "Computation<Row<Bg>, int> row_hash drifted — wire-format break.");
static_assert(
    row_hash_contribution_v<effects::Computation<
        Row<Effect::Alloc, Effect::IO>, int>>
 == 0x83D432DE6CDEACA7ULL,
    "Computation<Row<Alloc, IO>, int> row_hash drifted — break.");
static_assert(
    row_hash_contribution_v<effects::Computation<EmptyRow,
        effects::Computation<Row<Effect::IO>, int>>>
 == 0x94EC56B861A6B8FDULL,
    "Nested Computation<EmptyRow, Computation<Row<IO>, int>> "
    "row_hash drifted — wire-format break.  Inner-row "
    "non-collapsing through combine_ids must remain bit-stable.");

// ─── FIXY-V-055 — Witness<Tier, T> self-test deferred ─────────────
//
// `safety::Witness<Tier, T>` is the Observability-axis Graded carrier
// (CLAUDE.md §XVI).  Per-tier non-zero + pairwise distinctness +
// disjoint-from-Secret + non-sentinel invariants are pinned in the
// test_row_hash_distinctness.cpp matrix (FIXY-V-008), which fully-
// includes Witness.h (this header keeps `algebra::lattices::Witness`
// opaquely forward-declared so the federation-hash layer doesn't pull
// in the lattice definition).  The matrix's per-bucket distinctness
// + fold-anchor ceremony together discharge the entire Witness
// row-hash contract.

// ─── Bubble-sort helper correctness ────────────────────────────────

static_assert(detail::sorted_uints(std::array<std::uint64_t, 0>{})
           == std::array<std::uint64_t, 0>{});
static_assert(detail::sorted_uints(std::array<std::uint64_t, 1>{42})
           == std::array<std::uint64_t, 1>{42});
static_assert(detail::sorted_uints(std::array<std::uint64_t, 3>{3, 1, 2})
           == std::array<std::uint64_t, 3>{1, 2, 3});
static_assert(detail::sorted_uints(std::array<std::uint64_t, 4>{4, 3, 2, 1})
           == std::array<std::uint64_t, 4>{1, 2, 3, 4});
static_assert(detail::sorted_uints(std::array<std::uint64_t, 4>{1, 1, 1, 1})
           == std::array<std::uint64_t, 4>{1, 1, 1, 1});

// ─── unique_count_sorted helper correctness ─────────────────────────

static_assert(detail::unique_count_sorted(std::array<std::uint64_t, 0>{}) == 0);
static_assert(detail::unique_count_sorted(std::array<std::uint64_t, 1>{42}) == 1);
static_assert(detail::unique_count_sorted(std::array<std::uint64_t, 3>{1, 2, 3}) == 3);
static_assert(detail::unique_count_sorted(std::array<std::uint64_t, 4>{1, 1, 1, 1}) == 1);
static_assert(detail::unique_count_sorted(std::array<std::uint64_t, 4>{1, 1, 2, 2}) == 2);
static_assert(detail::unique_count_sorted(std::array<std::uint64_t, 5>{1, 1, 2, 3, 3}) == 3);
static_assert(detail::unique_count_sorted(std::array<std::uint64_t, 6>{0, 0, 1, 1, 2, 2}) == 3);

// ─── fmix64_fold_unique_sorted helper correctness ──────────────────
//
// Dedup-fold over a singleton array equals the non-dedup fold over
// the same singleton (xs[0] is always the first occurrence).  Dedup-
// fold over a duplicated singleton ([X, X]) equals fold over the
// canonical singleton ([X]).  These two invariants pin that the
// helper's "skip xs[i] == xs[i-1]" branch is wired the right way.

static_assert(
    detail::fmix64_fold_unique_sorted(std::array<std::uint64_t, 1>{7}, 0xAA)
 == detail::fmix64_fold(std::array<std::uint64_t, 1>{7}, 0xAA));

static_assert(
    detail::fmix64_fold_unique_sorted(std::array<std::uint64_t, 2>{7, 7}, 0xAA)
 == detail::fmix64_fold(std::array<std::uint64_t, 1>{7}, 0xAA));

static_assert(
    detail::fmix64_fold_unique_sorted(std::array<std::uint64_t, 3>{1, 1, 2}, 0xBB)
 == detail::fmix64_fold(std::array<std::uint64_t, 2>{1, 2}, 0xBB));

// Empty array — fold returns the seed verbatim regardless of dedup.
static_assert(
    detail::fmix64_fold_unique_sorted(std::array<std::uint64_t, 0>{}, 0xCC)
 == 0xCC);

}  // namespace detail::row_hash_self_test

}  // namespace crucible::safety::diag
