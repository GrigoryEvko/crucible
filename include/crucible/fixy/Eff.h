#pragma once

// ── crucible::fixy::eff — Met(X) effect-row substrate ──────────────
//
// Phase D re-export per misc/16_05_2026_fixy.md.  Surfaces the full
// effect-row machinery — `Row<Es...>`, `Computation<R, T>`, the
// Subrow concept, row set algebra, F* aliases (Pure/Tot/Ghost/Div/
// ST/All), capability tokens, ExecCtx, OsUniverse, EffectMask
// projection, ConcurrentRow, 21+ Resource budget tags — under
// `fixy::eff::` so callers who include only the fixy umbrella never
// have to descend into the effects/ tree.
//
// Per CLAUDE.md §XXI Universal Mint Pattern: there are no mints in
// this header — the carrier types (`Row`, `Computation`,
// `Capability`, `ExecCtx`) are the SUBSTRATE that the existing
// fixy/Cap.h minters (`mint_cap`, `mint_from_ctx`) build atop.  These
// re-exports preserve template identity exactly.
//
// ── Substrate consumed ─────────────────────────────────────────────
//
//   effects/Capabilities.h     — Effect enum, cap::*, Bg/Init/Test
//   effects/EffectRow.h        — Row<>, Subrow, row_union_t, etc.
//   effects/Computation.h      — Computation<R, T> carrier
//   effects/ComputationGraded.h
//                              — graded substrate for Computation
//   effects/EffectRowLattice.h — runtime-bitmap lattice
//   effects/EffectRowProjection.h
//                              — Row → EffectMask projection
//   effects/Capability.h       — Capability<E, S> + concept gates
//   effects/ExecCtx.h          — ExecCtx + canonical 5 ctxs
//   effects/Resources.h        — 21+ resource axis tags
//   effects/Concurrent.h       — ConcurrentRow<...> for budget sum
//   effects/OsUniverse.h       — Universe concept + OsUniverse
//   effects/CtxWrapperLift.h   — HotPath/AllocClass/Residency from ctx
//   effects/FxAliases.h        — Pure/Tot/Ghost/Div/ST/All rows
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe — every carrier ships NSDMI; aliases preserve discipline.
//   TypeSafe — using-declarations preserve concept gates: Subrow,
//              IsEffect, IsExecCtx, IsCapability, IsEffectRow,
//              ResourceTag, Universe.
//   NullSafe — carriers have no pointer state (cap::*, Bg, Init, Test
//              are all EBO-empty; Row is type-level only).
//   MemSafe  — Computation move-only via [[nodiscard]]; alias inherits.
//   BorrowSafe — cap::* tokens are move-typed; Bg/Init/Test are stack-
//              local context objects.
//   ThreadSafe — every cap-token consumer takes `Cap const&` to enforce
//              caller ownership.  alias preserves.
//   DetSafe  — Effect enum values are PINNED per FOUND-I04 append-only
//              Universe extension.  row_hash federation depends on bit-
//              for-bit stability; alias is zero-symbol so no drift.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  All re-exports are using-declarations + template aliases;
// no new instantiations, no new symbols.

#include <crucible/effects/Effects.h>        // umbrella: Capabilities,
                                             //   EffectRow, Computation,
                                             //   ComputationGraded,
                                             //   EffectRowLattice,
                                             //   EffectRowProjection,
                                             //   OsUniverse
#include <crucible/effects/Capability.h>     // Capability<E, S>
#include <crucible/effects/ExecCtx.h>        // ExecCtx + concepts
#include <crucible/effects/Resources.h>      // 21+ resource tags
#include <crucible/effects/Concurrent.h>     // ConcurrentRow
#include <crucible/effects/CtxWrapperLift.h> // HotPath/AllocClass/Residency from ctx
#include <crucible/effects/FxAliases.h>      // Pure/Tot/Ghost/Div/ST/All rows

namespace crucible::fixy::eff {

// ═══════════════════════════════════════════════════════════════════
// Effect atom catalog
// ═══════════════════════════════════════════════════════════════════

using ::crucible::effects::Effect;
using ::crucible::effects::IsEffect;
using ::crucible::effects::effect_count;
using ::crucible::effects::effect_name;

// ── cap::* tokens + top-level Alloc/IO/Block aliases ───────────────
namespace cap = ::crucible::effects::cap;

using ::crucible::effects::Alloc;
using ::crucible::effects::IO;
using ::crucible::effects::Block;

// ── Bg / Init / Test contexts (the cap-aggregator structs) ─────────
using ::crucible::effects::Bg;
using ::crucible::effects::Init;
using ::crucible::effects::Test;

// ═══════════════════════════════════════════════════════════════════
// Row<Es...> set algebra (EffectRow.h)
// ═══════════════════════════════════════════════════════════════════

template <Effect... Es>
using Row = ::crucible::effects::Row<Es...>;

using ::crucible::effects::EmptyRow;

template <typename R>
inline constexpr std::size_t row_size_v = ::crucible::effects::row_size_v<R>;

template <typename R, Effect E>
inline constexpr bool row_contains_v = ::crucible::effects::row_contains_v<R, E>;

template <typename R1, typename R2>
using row_union_t = ::crucible::effects::row_union_t<R1, R2>;

template <typename R1, typename R2>
using row_difference_t = ::crucible::effects::row_difference_t<R1, R2>;

template <typename R1, typename R2>
using row_intersection_t = ::crucible::effects::row_intersection_t<R1, R2>;

template <typename R1, typename R2>
inline constexpr bool is_subrow_v = ::crucible::effects::is_subrow_v<R1, R2>;

using ::crucible::effects::Subrow;     // concept

// ═══════════════════════════════════════════════════════════════════
// Computation<R, T> carrier
// ═══════════════════════════════════════════════════════════════════

template <typename R, typename T>
using Computation = ::crucible::effects::Computation<R, T>;

using ::crucible::effects::IsComputation;

// ComputationOverEmptyRow<T> — pure-row alias.  Substrate has the
// same alias in detail::computation_self_test only; we redefine
// publicly here for fixy:: callers.
template <typename T>
using ComputationOverEmptyRow = ::crucible::effects::Computation<
    ::crucible::effects::Row<>, T>;

// ═══════════════════════════════════════════════════════════════════
// EffectMask + Row → EffectMask projection (EffectRowProjection.h)
// ═══════════════════════════════════════════════════════════════════

using ::crucible::effects::EffectMask;
using ::crucible::effects::bits_for;
using ::crucible::effects::bits_from_row;

// ═══════════════════════════════════════════════════════════════════
// EffectRowLattice (runtime-bitmap lattice)
// ═══════════════════════════════════════════════════════════════════

using ::crucible::effects::EffectRowLattice;

template <typename R>
inline constexpr EffectRowLattice::element_type row_descriptor_v =
    ::crucible::effects::row_descriptor_v<R>;

// ═══════════════════════════════════════════════════════════════════
// Universe concept + OsUniverse
// ═══════════════════════════════════════════════════════════════════

using ::crucible::effects::Universe;
using ::crucible::effects::OsUniverse;

// ═══════════════════════════════════════════════════════════════════
// Capability<E, S> linear cap proof tokens
// ═══════════════════════════════════════════════════════════════════

template <Effect E, class Source>
using Capability = ::crucible::effects::Capability<E, Source>;

// Concept gates.
using ::crucible::effects::CanMintCap;
using ::crucible::effects::CtxCanMint;
using ::crucible::effects::IsCapability;
using ::crucible::effects::HasCapAndSource;
using ::crucible::effects::CapMatchesCtx;

// Traits.
template <class T, Effect E>
inline constexpr bool cap_matches_v = ::crucible::effects::cap_matches_v<T, E>;

template <class T>
inline constexpr Effect cap_of_v = ::crucible::effects::cap_of_v<T>;

template <class T>
using source_of_t = ::crucible::effects::source_of_t<T>;

template <class T>
inline constexpr bool is_capability_v = ::crucible::effects::is_capability_v<T>;

// Mints — also surfaced via fixy/Cap.h; re-aliased here for one-stop
// access from fixy::eff::.
using ::crucible::effects::mint_cap;
using ::crucible::effects::mint_from_ctx;

// ═══════════════════════════════════════════════════════════════════
// ExecCtx + canonical 5 contexts
// ═══════════════════════════════════════════════════════════════════

using ::crucible::effects::ExecCtx;

// Five canonical contexts.
using ::crucible::effects::HotFgCtx;
using ::crucible::effects::BgDrainCtx;
using ::crucible::effects::BgCompileCtx;
using ::crucible::effects::ColdInitCtx;
using ::crucible::effects::TestRunnerCtx;

// Concepts.
using ::crucible::effects::IsExecCtx;
using ::crucible::effects::HasCap;
using ::crucible::effects::HasNumaPolicy;
using ::crucible::effects::IsFgCtx;
using ::crucible::effects::IsBgCtx;
using ::crucible::effects::IsInitCtx;
using ::crucible::effects::IsTestCtx;
using ::crucible::effects::IsHotCtx;
using ::crucible::effects::IsWarmCtx;
using ::crucible::effects::IsColdCtx;
using ::crucible::effects::IsArenaCtx;
using ::crucible::effects::IsHugePageCtx;
using ::crucible::effects::IsHeapCtx;
using ::crucible::effects::IsStackCtx;
using ::crucible::effects::IsPoolCtx;
using ::crucible::effects::CtxAdmits;
using ::crucible::effects::IsSubCtx;
using ::crucible::effects::SiblingCtx;
using ::crucible::effects::CtxOwnsCapability;
using ::crucible::effects::IsCapType;
using ::crucible::effects::IsNumaPolicy;
using ::crucible::effects::IsAllocClass;
using ::crucible::effects::IsHeatTier;
using ::crucible::effects::IsResidencyTier;
using ::crucible::effects::IsEffectRow;
using ::crucible::effects::IsWorkloadHint;

// Ctx projection aliases.
template <IsExecCtx Ctx> using cap_type_of_t      = ::crucible::effects::cap_type_of_t<Ctx>;
template <IsExecCtx Ctx> using numa_policy_of_t   = ::crucible::effects::numa_policy_of_t<Ctx>;
template <IsExecCtx Ctx> using alloc_class_of_t   = ::crucible::effects::alloc_class_of_t<Ctx>;
template <IsExecCtx Ctx> using hot_path_tier_of_t = ::crucible::effects::hot_path_tier_of_t<Ctx>;
template <IsExecCtx Ctx> using residency_of_t     = ::crucible::effects::residency_of_t<Ctx>;
template <IsExecCtx Ctx> using row_type_of_t      = ::crucible::effects::row_type_of_t<Ctx>;
template <IsExecCtx Ctx> using workload_hint_of_t = ::crucible::effects::workload_hint_of_t<Ctx>;

// Ctx tag namespaces.
namespace ctx_cap      = ::crucible::effects::ctx_cap;
namespace ctx_numa     = ::crucible::effects::ctx_numa;
namespace ctx_alloc    = ::crucible::effects::ctx_alloc;
namespace ctx_heat     = ::crucible::effects::ctx_heat;
namespace ctx_resid    = ::crucible::effects::ctx_resid;
namespace ctx_workload = ::crucible::effects::ctx_workload;

// Ctx-driven wrapper lifts.
using ::crucible::effects::HotPathFromCtx;
using ::crucible::effects::AllocClassFromCtx;
using ::crucible::effects::ResidencyHeatFromCtx;

// ═══════════════════════════════════════════════════════════════════
// Resources (21+ resource axis budget tags)
// ═══════════════════════════════════════════════════════════════════

using ::crucible::effects::ResourceTag;
using ::crucible::effects::IsResourceKind;
using ::crucible::effects::ResourceKind;
using ::crucible::effects::resource_kind_count;

namespace resource = ::crucible::effects::resource;

// Short top-level aliases.
using ::crucible::effects::SmBudget;
using ::crucible::effects::WarpSchedulerSlots;
using ::crucible::effects::RegistersPerWarp;
using ::crucible::effects::SmemBytes;
using ::crucible::effects::L2Bytes;
using ::crucible::effects::HbmBytes;
using ::crucible::effects::HbmBandwidth;
using ::crucible::effects::NvlinkBandwidth;
using ::crucible::effects::PcieBandwidth;
using ::crucible::effects::NicQueueBudget;
using ::crucible::effects::NicRingDepth;
using ::crucible::effects::NicQp;
using ::crucible::effects::NicCq;
using ::crucible::effects::NicMr;
using ::crucible::effects::SwitchEgressBw;
using ::crucible::effects::SwitchBufferCells;
using ::crucible::effects::TcamEntries;
using ::crucible::effects::CpuCoreBudget;
using ::crucible::effects::LlcBytes;
using ::crucible::effects::PowerWatts;
using ::crucible::effects::ThermalCelsius;
using ::crucible::effects::RackPowerKw;
using ::crucible::effects::CarbonGramsPerKwh;

// ═══════════════════════════════════════════════════════════════════
// ConcurrentRow + per-kind budget sum
// ═══════════════════════════════════════════════════════════════════

template <ResourceTag... Tags>
using ConcurrentRow = ::crucible::effects::ConcurrentRow<Tags...>;

template <ResourceKind K, typename R>
inline constexpr std::uint64_t concurrent_row_value_v =
    ::crucible::effects::concurrent_row_value_v<K, R>;

// ═══════════════════════════════════════════════════════════════════
// F* alias rows (Pure / Tot / Ghost / Div / ST / All)
// ═══════════════════════════════════════════════════════════════════

using ::crucible::effects::PureRow;
using ::crucible::effects::TotRow;
using ::crucible::effects::GhostRow;
using ::crucible::effects::DivRow;
using ::crucible::effects::STRow;
using ::crucible::effects::AllRow;

// F* refinement-chain concepts.
using ::crucible::effects::IsPure;
using ::crucible::effects::IsTot;
using ::crucible::effects::IsGhost;
using ::crucible::effects::IsDiv;
using ::crucible::effects::IsST;
using ::crucible::effects::IsAll;

}  // namespace crucible::fixy::eff

// ── Self-test ──────────────────────────────────────────────────────
//
// Witness that every alias preserves substrate identity.  Full
// coverage in test_fixy_eff.cpp.

namespace crucible::fixy::eff::self_test {

// Effect enum identity.
static_assert(Effect::Alloc == ::crucible::effects::Effect::Alloc);
static_assert(Effect::Bg    == ::crucible::effects::Effect::Bg);
static_assert(effect_count  == ::crucible::effects::effect_count);

// Row template identity.
static_assert(std::is_same_v<Row<Effect::Alloc>,
                             ::crucible::effects::Row<::crucible::effects::Effect::Alloc>>,
    "fixy::eff::Row must alias effects::Row");

// Subrow concept passes through.
static_assert(Subrow<Row<>, Row<Effect::Alloc>>);

// Computation carrier identity.
static_assert(std::is_same_v<Computation<Row<>, int>,
                             ::crucible::effects::Computation<
                                 ::crucible::effects::Row<>, int>>,
    "fixy::eff::Computation must alias effects::Computation");

// Capability identity.
static_assert(std::is_same_v<Capability<Effect::Alloc, Bg>,
                             ::crucible::effects::Capability<
                                 ::crucible::effects::Effect::Alloc,
                                 ::crucible::effects::Bg>>,
    "fixy::eff::Capability must alias effects::Capability");

// F* aliases preserved.
static_assert(IsPure<PureRow>);
static_assert(IsTot<TotRow>);
static_assert(IsST<STRow>);
static_assert(IsAll<AllRow>);

// ExecCtx concepts pass through.
static_assert(IsExecCtx<HotFgCtx>);
static_assert(IsExecCtx<BgDrainCtx>);
static_assert(IsBgCtx<BgDrainCtx>);
static_assert(IsFgCtx<HotFgCtx>);

// Context layout invariants preserved (one byte each).
static_assert(sizeof(Bg)   == 1);
static_assert(sizeof(Init) == 1);
static_assert(sizeof(Test) == 1);

}  // namespace crucible::fixy::eff::self_test
