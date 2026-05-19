#pragma once

// ── crucible::fixy::eff — Met(X) effect-row substrate ──────────────
//
// Phase D re-export per misc/16_05_2026_fixy.md.  Surfaces the full
// effect-row machinery — `Row<Es...>`, `Computation<R, T>`, the
// Subrow concept, row set algebra, F* aliases (Pure/Tot/Ghost/Div/
// ST/All), capability tokens, ExecCtx, OsUniverse, EffectMask
// projection, ConcurrentRow, 23 ResourceKind budget tags — under
// `fixy::eff::` so callers who include only the fixy umbrella never
// have to descend into the effects/ tree.  Cardinality is pinned by
// the substrate's `static_assert(resource_kind_count == 23)` in
// effects/Resources.h:430 AND mirrored by the fixy-side witness in
// the self-test block below.
//
// Per CLAUDE.md §XXI Universal Mint Pattern: there are no mints in
// this header — the carrier types (`Row`, `Computation`,
// `Capability`, `ExecCtx`) are the SUBSTRATE that the existing
// fixy/Cap.h minters (`mint_cap`, `mint_from_ctx`) build atop.
//
// ── Surface form (FIXY-AUDIT-B12 decision) ─────────────────────────
//
// **Primary form: namespace alias.**  The single line
//
//     namespace fixy::eff = ::crucible::effects;
//
// makes `fixy::eff::*` an EXACT synonym for `crucible::effects::*` —
// every name in the substrate (Row, Computation, Capability,
// ExecCtx, HotFgCtx/BgDrainCtx/BgCompileCtx/ColdInitCtx/TestRunnerCtx,
// the 21+ resource tags, F* aliases, every concept gate, every cap::*
// tag, every ctx_* sub-namespace) is reachable through `fixy::eff::`
// without an explicit `using`-decl listing each name.
//
// **Why a namespace alias instead of 50+ using-decls.**  The previous
// shape of this header enumerated ~109 individual `using ::crucible::
// effects::X;` decls plus 7 template-alias redeclarations.  Each
// using-decl was a maintenance burden — every new substrate symbol
// (a new ResourceKind, a new ctx-projection trait, a new F* alias)
// required a parallel addition in Eff.h or it stayed invisible to
// fixy:: callers.  The 16_05_2026_fixy.md plan budgeted ~80 LoC for
// this header; the namespace-alias form lands well within budget AND
// inherits future substrate additions for free.  Template-alias
// redeclarations (Row, Computation, Capability, row_*_t,
// ConcurrentRow, etc.) become redundant — they already lived in
// `crucible::effects`, and the alias makes them reachable as
// `fixy::eff::Row<...>` automatically.
//
// **Why explicit using-decls would be wrong here.**  Adding a `using
// crucible::effects::Bg;` (or `IO`, `Init`, `Test`, etc.) at the
// fixy::eff:: scope on TOP of the namespace alias would attempt to
// re-declare the same symbol in the same namespace — an ODR error,
// not just a redundancy.  Callers who want a fixy-namespaced spelling
// of a load-bearing tag (typically for grep / diagnostic-name reasons)
// still get it: `fixy::eff::Bg` IS `crucible::effects::Bg` under the
// alias.  No separate using-decl pile required.
//
// ── Substrate consumed ─────────────────────────────────────────────
//
//   effects/Effects.h umbrella covers:
//     Capabilities.h  — Effect enum, cap::*, Bg/Init/Test
//     EffectRow.h     — Row<>, Subrow, row_union_t, row_difference_t,
//                       row_intersection_t, row_size_v, row_contains_v
//     Computation.h   — Computation<R, T> carrier + IsComputation
//     ComputationGraded.h
//                     — graded substrate for Computation
//     EffectRowLattice.h
//                     — runtime-bitmap lattice
//     EffectRowProjection.h
//                     — Row → EffectMask projection
//     OsUniverse.h    — Universe concept + OsUniverse
//
//   Plus the standalone effects/ headers:
//     Capability.h    — Capability<E, S> + CanMintCap / IsCapability /
//                       HasCapAndSource / CapMatchesCtx + mint_cap
//     ExecCtx.h       — ExecCtx + canonical 5 ctxs (HotFgCtx,
//                       BgDrainCtx, BgCompileCtx, ColdInitCtx,
//                       TestRunnerCtx) + IsExecCtx / IsFgCtx / IsBgCtx
//                       / IsInitCtx / IsTestCtx + ctx_cap / ctx_numa /
//                       ctx_alloc / ctx_heat / ctx_resid /
//                       ctx_workload + cap_type_of_t / numa_policy_of_t
//                       / alloc_class_of_t / hot_path_tier_of_t /
//                       residency_of_t / row_type_of_t /
//                       workload_hint_of_t + mint_from_ctx
//     Resources.h     — 23 resource::* budget tag templates (SmBudget,
//                       WarpSchedulerSlots, RegistersPerWarp, SmemBytes,
//                       L2Bytes, HbmBytes, HbmBandwidth, NvlinkBandwidth,
//                       PcieBandwidth, NicQueueBudget, NicRingDepth,
//                       NicQp, NicCq, NicMr, SwitchEgressBw,
//                       SwitchBufferCells, TcamEntries, CpuCoreBudget,
//                       LlcBytes, PowerWatts, ThermalCelsius,
//                       RackPowerKw, CarbonGramsPerKwh) over 23
//                       ResourceKind atoms (Sm, WarpScheduler, ...
//                       same atom list — see effects/Resources.h:140)
//                       + ResourceTag concept + resource_kind_count
//     Concurrent.h    — ConcurrentRow<Tags...> + concurrent_row_value_v
//     CtxWrapperLift.h — HotPathFromCtx / AllocClassFromCtx /
//                       ResidencyHeatFromCtx
//     FxAliases.h     — PureRow / TotRow / GhostRow / DivRow / STRow /
//                       AllRow + IsPure / IsTot / IsGhost / IsDiv /
//                       IsST / IsAll
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe — every carrier ships NSDMI in the substrate; namespace
//              alias inherits.
//   TypeSafe — concept gates (Subrow, IsEffect, IsExecCtx, IsCapability,
//              IsEffectRow, ResourceTag, Universe) reachable verbatim.
//   NullSafe — carriers have no pointer state (cap::*, Bg, Init, Test
//              are EBO-empty; Row is type-level only).
//   MemSafe  — Computation move-only via [[nodiscard]]; alias inherits.
//   BorrowSafe — cap::* tokens are move-typed; Bg/Init/Test are stack-
//              local context objects.
//   ThreadSafe — every cap-token consumer takes `Cap const&` to enforce
//              caller ownership.
//   DetSafe  — Effect enum values are PINNED per FOUND-I04 append-only
//              Universe extension.  row_hash federation depends on
//              bit-for-bit stability; alias is zero-symbol so no drift.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  A namespace alias is a pure name-lookup directive; no new
// symbols, no new instantiations, no new ABI surface.

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

#include <type_traits>

namespace crucible::fixy {
namespace eff = ::crucible::effects;
}  // namespace crucible::fixy

// ── Self-test ──────────────────────────────────────────────────────
//
// Witness that the namespace alias preserves substrate identity.
// Full coverage in test_fixy_eff.cpp + the load-bearing-symbol
// alias test test_fixy_eff_alias.cpp (FIXY-AUDIT-B12 HS14 floor).

namespace crucible::fixy::eff_self_test {

// Effect enum identity through the alias.
static_assert(::crucible::fixy::eff::Effect::Alloc
           == ::crucible::effects::Effect::Alloc);
static_assert(::crucible::fixy::eff::Effect::Bg
           == ::crucible::effects::Effect::Bg);
static_assert(::crucible::fixy::eff::effect_count
           == ::crucible::effects::effect_count);

// Row template identity through the alias.
static_assert(std::is_same_v<
    ::crucible::fixy::eff::Row<::crucible::effects::Effect::Alloc>,
    ::crucible::effects::Row<::crucible::effects::Effect::Alloc>>,
    "fixy::eff::Row must BE effects::Row under the namespace alias.");

// Subrow concept passes through.
static_assert(::crucible::fixy::eff::Subrow<
    ::crucible::fixy::eff::Row<>,
    ::crucible::fixy::eff::Row<::crucible::effects::Effect::Alloc>>);

// Computation carrier identity through the alias.
static_assert(std::is_same_v<
    ::crucible::fixy::eff::Computation<::crucible::fixy::eff::Row<>, int>,
    ::crucible::effects::Computation<::crucible::effects::Row<>, int>>,
    "fixy::eff::Computation must BE effects::Computation.");

// Capability identity.
static_assert(std::is_same_v<
    ::crucible::fixy::eff::Capability<
        ::crucible::effects::Effect::Alloc,
        ::crucible::fixy::eff::Bg>,
    ::crucible::effects::Capability<
        ::crucible::effects::Effect::Alloc,
        ::crucible::effects::Bg>>,
    "fixy::eff::Capability must BE effects::Capability.");

// F* aliases preserved.
static_assert(::crucible::fixy::eff::IsPure<::crucible::fixy::eff::PureRow>);
static_assert(::crucible::fixy::eff::IsTot<::crucible::fixy::eff::TotRow>);
static_assert(::crucible::fixy::eff::IsST<::crucible::fixy::eff::STRow>);
static_assert(::crucible::fixy::eff::IsAll<::crucible::fixy::eff::AllRow>);

// ExecCtx concepts pass through.
static_assert(::crucible::fixy::eff::IsExecCtx<::crucible::fixy::eff::HotFgCtx>);
static_assert(::crucible::fixy::eff::IsExecCtx<::crucible::fixy::eff::BgDrainCtx>);
static_assert(::crucible::fixy::eff::IsBgCtx<::crucible::fixy::eff::BgDrainCtx>);
static_assert(::crucible::fixy::eff::IsFgCtx<::crucible::fixy::eff::HotFgCtx>);

// Context layout invariants preserved (one byte each).
static_assert(sizeof(::crucible::fixy::eff::Bg)   == 1);
static_assert(sizeof(::crucible::fixy::eff::Init) == 1);
static_assert(sizeof(::crucible::fixy::eff::Test) == 1);

// fixy-M-05: ResourceKind cardinality witness — couples the docstring
// claim "23 ResourceKind budget tags" to the substrate truth.  The
// substrate already pins resource_kind_count via reflection, but a
// fixy-side mirror guarantees that an underbump in the substrate (or a
// new enumerator without a docstring update) breaks the build at the
// fixy layer too.  Adding a 24th ResourceKind requires extending BOTH
// the eff_self_test block AND the docstring tag list above.
static_assert(::crucible::fixy::eff::resource_kind_count == 23,
    "fixy::eff::resource_kind_count diverged from 23 — extend the "
    "ResourceKind tag list in the Eff.h docstring in lockstep with "
    "the substrate change.");

// Per-tag identity for every resource::* budget template — proves
// the namespace alias surfaces every shipped tag (not just a sampled
// subset) and that adding a new tag forces a parallel update here
// for the new fixy::eff::resource::*<N> instantiation to resolve.
// Each tag is instantiated with N=1 as a witness — the structural
// identity holds independent of N.
static_assert(std::is_same_v<::crucible::fixy::eff::resource::SmBudget<1>,           ::crucible::effects::resource::SmBudget<1>>);
static_assert(std::is_same_v<::crucible::fixy::eff::resource::WarpSchedulerSlots<1>, ::crucible::effects::resource::WarpSchedulerSlots<1>>);
static_assert(std::is_same_v<::crucible::fixy::eff::resource::RegistersPerWarp<1>,   ::crucible::effects::resource::RegistersPerWarp<1>>);
static_assert(std::is_same_v<::crucible::fixy::eff::resource::SmemBytes<1>,          ::crucible::effects::resource::SmemBytes<1>>);
static_assert(std::is_same_v<::crucible::fixy::eff::resource::L2Bytes<1>,            ::crucible::effects::resource::L2Bytes<1>>);
static_assert(std::is_same_v<::crucible::fixy::eff::resource::HbmBytes<1>,           ::crucible::effects::resource::HbmBytes<1>>);
static_assert(std::is_same_v<::crucible::fixy::eff::resource::HbmBandwidth<1>,       ::crucible::effects::resource::HbmBandwidth<1>>);
static_assert(std::is_same_v<::crucible::fixy::eff::resource::NvlinkBandwidth<1>,    ::crucible::effects::resource::NvlinkBandwidth<1>>);
static_assert(std::is_same_v<::crucible::fixy::eff::resource::PcieBandwidth<1>,      ::crucible::effects::resource::PcieBandwidth<1>>);
static_assert(std::is_same_v<::crucible::fixy::eff::resource::NicQueueBudget<1>,     ::crucible::effects::resource::NicQueueBudget<1>>);
static_assert(std::is_same_v<::crucible::fixy::eff::resource::NicRingDepth<1>,       ::crucible::effects::resource::NicRingDepth<1>>);
static_assert(std::is_same_v<::crucible::fixy::eff::resource::NicQp<1>,              ::crucible::effects::resource::NicQp<1>>);
static_assert(std::is_same_v<::crucible::fixy::eff::resource::NicCq<1>,              ::crucible::effects::resource::NicCq<1>>);
static_assert(std::is_same_v<::crucible::fixy::eff::resource::NicMr<1>,              ::crucible::effects::resource::NicMr<1>>);
static_assert(std::is_same_v<::crucible::fixy::eff::resource::SwitchEgressBw<1>,     ::crucible::effects::resource::SwitchEgressBw<1>>);
static_assert(std::is_same_v<::crucible::fixy::eff::resource::SwitchBufferCells<1>,  ::crucible::effects::resource::SwitchBufferCells<1>>);
static_assert(std::is_same_v<::crucible::fixy::eff::resource::TcamEntries<1>,        ::crucible::effects::resource::TcamEntries<1>>);
static_assert(std::is_same_v<::crucible::fixy::eff::resource::CpuCoreBudget<1>,      ::crucible::effects::resource::CpuCoreBudget<1>>);
static_assert(std::is_same_v<::crucible::fixy::eff::resource::LlcBytes<1>,           ::crucible::effects::resource::LlcBytes<1>>);
static_assert(std::is_same_v<::crucible::fixy::eff::resource::PowerWatts<1>,         ::crucible::effects::resource::PowerWatts<1>>);
static_assert(std::is_same_v<::crucible::fixy::eff::resource::ThermalCelsius<1>,     ::crucible::effects::resource::ThermalCelsius<1>>);
static_assert(std::is_same_v<::crucible::fixy::eff::resource::RackPowerKw<1>,        ::crucible::effects::resource::RackPowerKw<1>>);
static_assert(std::is_same_v<::crucible::fixy::eff::resource::CarbonGramsPerKwh<1>,  ::crucible::effects::resource::CarbonGramsPerKwh<1>>);

}  // namespace crucible::fixy::eff_self_test
