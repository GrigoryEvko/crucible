#pragma once

// ── crucible::concurrent::SubstrateCtxFit ───────────────────────────
//
// Cross-tree composition: does a Substrate's HOT-PATH access pattern
// fit the residency tier claimed by an ExecCtx?  This extends the
// existing ExecCtx heat × resid coherence rule (which catches Heat=
// Hot + Resid=DRAM) one level deeper — but the fit metric is
// per_call_working_set_v<S>, NOT total channel storage.
//
// ── The two-metric distinction (load-bearing) ──────────────────────
//
// A SpscRing's producer touches O(1) cache lines per try_push call
// regardless of capacity (head + tail + 1 destination cell).  An
// SPSC<int, 1M> with 4 MB total storage has the SAME hot-path
// access footprint as an SPSC<int, 64> with 256 B total: ~192 B,
// L1d-resident on every supported host.
//
// Therefore:
//
//   * SubstrateFitsCtxResidency<S, Ctx>
//       — The HARD GATE on hot-path access.  Uses
//         per_call_working_set_v<S> from concurrent/Substrate.h.
//         Production functions parameterized over (Substrate, Ctx)
//         include this concept as a requires-clause to reject
//         INCOHERENT pairings (e.g., a 64-KB-cell substrate paired
//         with HotFgCtx whose L1d budget is 32 KB).
//
//   * StorageFitsCtxResidency<S, Ctx>
//       — A SOFTER, OPTIONAL gate on TOTAL channel storage.  Uses
//         channel_byte_footprint_v<S>.  Right for callers in
//         construction / cold-init / arena-placement contexts where
//         the WHOLE channel buffer is touched (mmap setup, hugepage
//         residency hint, NUMA placement, drain-everything scan).
//         A 4 MB SpscRing legitimately needs ColdInitCtx for
//         construction even though its hot-path use is L1-resident.
//
//   * SubstrateBenefitsFromParallelism<S>
//       — TRUE iff channel_byte_footprint_v<S> exceeds the
//         conservative L2/core bound.  Communicates "above the
//         cliff — this workload could benefit from sharding /
//         parallelization" at the type level.  See ParallelismRule
//         (concurrent/ParallelismRule.h) for the corresponding
//         runtime decision rule.
//
// ── Why the original conflation was wrong ──────────────────────────
//
// The pre-#861 SubstrateFitsCtxResidency used channel_byte_footprint_v
// (TOTAL storage) and rejected (LargeSpsc, HotFgCtx).  But the hot
// path on LargeSpsc IS L1d-fitting; the gate confused storage class
// with hot-path access pattern.  The fix splits the two metrics
// into distinct concepts so each callsite picks the right question.
//
// ── Conservative cache-size bounds ──────────────────────────────────
//
// The actual cache hierarchy is queried at runtime from
// concurrent::Topology singleton (per-host: Zen 4 has 32 KB L1d,
// Sapphire Rapids has 48 KB, Graviton 3 has 64 KB; etc.).  These
// constants are SAFE LOWER BOUNDS — values smaller than every
// supported host's actual cache size.  A "fits in tier" check
// using these bounds errs on the side of "doesn't fit, bump up a
// tier" — sound under-approximation: the static check rejects
// MORE configurations than the runtime check would, never fewer.
//
// If a future target (e.g., a smaller embedded class) drops below
// these floors, the bound must be tightened here AND callers
// re-audited.

#include <crucible/concurrent/ExecCtxBridge.h>         // ctx_residency_tier<Ctx>()
#include <crucible/concurrent/ParallelismRule.h>       // Tier enum
#include <crucible/concurrent/Substrate.h>             // ChannelTopology + extractors
                                                       // + per_call_working_set_v
#include <crucible/effects/ExecCtx.h>                  // IsExecCtx + ctx_residency_tier_of_t

#include <cstddef>

namespace crucible::concurrent {

inline constexpr std::size_t conservative_l1d_per_core =      32 * 1024;   //  32 KB
inline constexpr std::size_t conservative_l2_per_core  =     256 * 1024;   // 256 KB
inline constexpr std::size_t conservative_l3_total     = 16 * 1024 * 1024; //  16 MB

// ── fits_in_tier_v ──────────────────────────────────────────────────
//
// Does a byte footprint fit in the requested cache tier?  DRAMBound
// always fits (anything fits DRAM).  Other tiers compare against
// the conservative bounds above.

template <std::size_t Footprint, Tier T>
inline constexpr bool fits_in_tier_v = [] consteval {
    if constexpr (T == Tier::L1Resident)
        return Footprint <= conservative_l1d_per_core;
    else if constexpr (T == Tier::L2Resident)
        return Footprint <= conservative_l2_per_core;
    else if constexpr (T == Tier::L3Resident)
        return Footprint <= conservative_l3_total;
    else /* T == Tier::DRAMBound */
        return true;
}();

// ── SubstrateFitsCtxResidency ──────────────────────────────────────
//
// HARD GATE — production functions parameterized over (Substrate,
// Ctx) include this concept as a requires-clause.
//
// Uses per_call_working_set_v<S>: the bytes the producer/consumer's
// hot path actually touches per try_send / try_recv / publish / load
// call.  Independent of total ring capacity — what matters is whether
// the head/tail counters + one destination cell fit the ctx's
// residency tier.
//
// Most pairings pass: every Permissioned* primitive's per-call WS
// is ≤ ~5 cache lines (~320 B), well under the L1d conservative
// bound (32 KB).  The gate fires when sizeof(value_type) is so
// large that one cell overflows the tier (e.g., a 64-KB struct in
// HotFgCtx legitimately fails).

template <class S, class Ctx>
concept SubstrateFitsCtxResidency =
    IsSubstrate<S>
 && ::crucible::effects::IsExecCtx<Ctx>
 && fits_in_tier_v<per_call_working_set_v<S>, ctx_residency_tier<Ctx>()>;

// ── StorageFitsCtxResidency ────────────────────────────────────────
//
// COLD-INIT / TOTAL-STORAGE gate.  Uses channel_byte_footprint_v<S>:
// the entire ring buffer or Snapshot slot.  Right for construction-
// time decisions (allocator placement, NUMA pinning, hugepage hint,
// full-scan APIs).  Production hot-path code uses
// SubstrateFitsCtxResidency instead.
//
// Pairs with ColdInitCtx (DRAMBound) by default — anything fits
// DRAM.  Tighter ctxs (L3-bound) are valid for moderate-size
// channels; HotFgCtx is rarely valid here (would mean the ENTIRE
// ring fits L1d, which only tiny channels do).

template <class S, class Ctx>
concept StorageFitsCtxResidency =
    IsSubstrate<S>
 && ::crucible::effects::IsExecCtx<Ctx>
 && fits_in_tier_v<channel_byte_footprint_v<S>, ctx_residency_tier<Ctx>()>;

// ── SubstrateBenefitsFromParallelism ───────────────────────────────
//
// TRUE iff total channel storage crosses the conservative L2/core
// bound (256 KB) — the cliff per ParallelismRule (concurrent/
// ParallelismRule.h).  Above the cliff, sharding / parallelization
// pays for itself; below, sequential is strictly faster (cache
// invalidation traffic > parallel work).
//
// USE: type-level signal that callers can branch on to recommend
// sharded variants:
//
//   if constexpr (SubstrateBenefitsFromParallelism<S>) {
//       // suggest ShardedSpscGrid / PermissionedShardedGrid
//       // alternative; or wire NumaSpread placement
//   }
//
// NOT a hard rejection — single-producer-single-consumer above the
// cliff (TraceRing-style) is a perfectly valid configuration; just
// means the developer should be aware of the alternative.

template <class S>
concept SubstrateBenefitsFromParallelism =
    IsSubstrate<S>
 && (channel_byte_footprint_v<S> > conservative_l2_per_core);

// ── Inverse: residency_tier_required_for ────────────────────────────
//
// Given a Substrate's TOTAL footprint, which is the smallest (most
// permissive) Tier that fits?  Used by callers wanting to derive
// the construction-ctx's required residency from the Substrate
// rather than constraining a pre-existing Ctx.  Mirrors
// StorageFitsCtxResidency's metric (channel_byte_footprint_v).

template <std::size_t Footprint>
inline constexpr Tier required_tier_for_footprint = [] consteval {
    if      (Footprint <= conservative_l1d_per_core) return Tier::L1Resident;
    else if (Footprint <= conservative_l2_per_core)  return Tier::L2Resident;
    else if (Footprint <= conservative_l3_total)     return Tier::L3Resident;
    else                                              return Tier::DRAMBound;
}();

template <IsSubstrate S>
inline constexpr Tier substrate_required_tier_v =
    required_tier_for_footprint<channel_byte_footprint_v<S>>;

// Mirror for hot-path side: smallest Tier the per-call WS fits.
// Almost always L1Resident for any Permissioned* substrate; useful
// for completeness and as the inverse of SubstrateFitsCtxResidency.

template <IsSubstrate S>
inline constexpr Tier substrate_hot_path_required_tier_v =
    required_tier_for_footprint<per_call_working_set_v<S>>;

// ── Self-test block ─────────────────────────────────────────────────
namespace detail::substrate_ctx_fit_self_test {

namespace eff = ::crucible::effects;

struct UserTag {};

// SPSC<int, 1024> = 4 KB total / 192 B per-call.  Both L1-resident.
using SmallSpsc = Substrate_t<ChannelTopology::OneToOne, int, 1024, UserTag>;
static_assert(channel_byte_footprint_v<SmallSpsc> == 4 * 1024);
static_assert( fits_in_tier_v<4 * 1024, Tier::L1Resident>);
static_assert( fits_in_tier_v<4 * 1024, Tier::L2Resident>);
static_assert( fits_in_tier_v<4 * 1024, Tier::DRAMBound>);

// SPSC<int, 65536> = 256 KB total / 192 B per-call.
using BoundarySpsc = Substrate_t<ChannelTopology::OneToOne, int, 65536, UserTag>;
static_assert(channel_byte_footprint_v<BoundarySpsc> == 256 * 1024);
static_assert(!fits_in_tier_v<256 * 1024, Tier::L1Resident>);   // > 32 KB
static_assert( fits_in_tier_v<256 * 1024, Tier::L2Resident>);   // == 256 KB

// SPSC<int, 1M> = 4 MB total / 192 B per-call.
using LargeSpsc = Substrate_t<ChannelTopology::OneToOne, int, 1024 * 1024, UserTag>;
static_assert(channel_byte_footprint_v<LargeSpsc> == 4 * 1024 * 1024);
static_assert(!fits_in_tier_v<4 * 1024 * 1024, Tier::L1Resident>);
static_assert(!fits_in_tier_v<4 * 1024 * 1024, Tier::L2Resident>);
static_assert( fits_in_tier_v<4 * 1024 * 1024, Tier::L3Resident>);

// Snapshot<double> = 8 B → fits everywhere.
using SnapT = Substrate_t<ChannelTopology::OneToMany_Latest, double, 0, UserTag>;
static_assert(channel_byte_footprint_v<SnapT> == sizeof(double));
static_assert(fits_in_tier_v<sizeof(double), Tier::L1Resident>);

// ── SubstrateFitsCtxResidency on canonical contexts ─────────────────
//
// THE LOAD-BEARING POST-FIX BEHAVIOR: per-call WS is L1-resident
// for every Permissioned* primitive in the zoo, INDEPENDENT of
// total capacity.  All three (Small/Boundary/Large) fit HotFgCtx.

static_assert( SubstrateFitsCtxResidency<SmallSpsc,    eff::HotFgCtx>);
static_assert( SubstrateFitsCtxResidency<BoundarySpsc, eff::HotFgCtx>);
static_assert( SubstrateFitsCtxResidency<LargeSpsc,    eff::HotFgCtx>);

static_assert( SubstrateFitsCtxResidency<SmallSpsc,    eff::BgDrainCtx>);
static_assert( SubstrateFitsCtxResidency<BoundarySpsc, eff::BgDrainCtx>);
static_assert( SubstrateFitsCtxResidency<LargeSpsc,    eff::BgDrainCtx>);

static_assert( SubstrateFitsCtxResidency<SmallSpsc,    eff::ColdInitCtx>);
static_assert( SubstrateFitsCtxResidency<BoundarySpsc, eff::ColdInitCtx>);
static_assert( SubstrateFitsCtxResidency<LargeSpsc,    eff::ColdInitCtx>);

// Snapshot<double> trivially fits everywhere.
static_assert( SubstrateFitsCtxResidency<SnapT, eff::HotFgCtx>);

// ── Per-call WS gate FIRES when sizeof(value_type) is huge ─────────
//
// A 64-KB struct cell genuinely overflows L1d (which is 32 KB) and
// L2 (256 KB conservative).  This is what the per-call WS gate
// rejects — the kind of pairing that DOES degrade hot-path latency.

struct alignas(64) Big {
    char buf[64 * 1024];   // 64 KB cell — exceeds L1d alone
    auto operator<=>(Big const&) const = default;
};

using BigCellSpsc = Substrate_t<ChannelTopology::OneToOne, Big, 4, UserTag>;
// per-call WS: 2 lines (head/tail) + 64 KB cell padded = 65664 B
//   → exceeds L1d (32 KB), exceeds L2 (256 KB)? no, 65 KB fits L2
static_assert(per_call_working_set_v<BigCellSpsc> >  conservative_l1d_per_core);
static_assert(per_call_working_set_v<BigCellSpsc> <= conservative_l2_per_core);

static_assert(!SubstrateFitsCtxResidency<BigCellSpsc, eff::HotFgCtx>);
static_assert( SubstrateFitsCtxResidency<BigCellSpsc, eff::BgDrainCtx>);
static_assert( SubstrateFitsCtxResidency<BigCellSpsc, eff::ColdInitCtx>);

// ── StorageFitsCtxResidency on canonical contexts ──────────────────
//
// Pre-#861 SubstrateFitsCtxResidency behavior — total-storage gate.
// Use this when the caller IS scanning / placing / hugepage-hinting
// the whole channel.

static_assert( StorageFitsCtxResidency<SmallSpsc,    eff::HotFgCtx>);     // 4 KB ≤ 32 KB
static_assert(!StorageFitsCtxResidency<BoundarySpsc, eff::HotFgCtx>);     // 256 KB > 32 KB
static_assert(!StorageFitsCtxResidency<LargeSpsc,    eff::HotFgCtx>);     // 4 MB > 32 KB

static_assert( StorageFitsCtxResidency<SmallSpsc,    eff::BgDrainCtx>);
static_assert( StorageFitsCtxResidency<BoundarySpsc, eff::BgDrainCtx>);
static_assert(!StorageFitsCtxResidency<LargeSpsc,    eff::BgDrainCtx>);

static_assert( StorageFitsCtxResidency<SmallSpsc,    eff::ColdInitCtx>);
static_assert( StorageFitsCtxResidency<BoundarySpsc, eff::ColdInitCtx>);
static_assert( StorageFitsCtxResidency<LargeSpsc,    eff::ColdInitCtx>);

// ── SubstrateBenefitsFromParallelism — the cliff signal ────────────

static_assert(!SubstrateBenefitsFromParallelism<SmallSpsc>);     // 4 KB < cliff
static_assert(!SubstrateBenefitsFromParallelism<BoundarySpsc>);  // 256 KB == cliff (not >)
static_assert( SubstrateBenefitsFromParallelism<LargeSpsc>);     // 4 MB > cliff
static_assert(!SubstrateBenefitsFromParallelism<SnapT>);         // 8 B < cliff

// ── required_tier_for_footprint inverse mapping ────────────────────

static_assert(required_tier_for_footprint<sizeof(double)>     == Tier::L1Resident);
static_assert(required_tier_for_footprint<32 * 1024>           == Tier::L1Resident);   // boundary
static_assert(required_tier_for_footprint<32 * 1024 + 1>       == Tier::L2Resident);
static_assert(required_tier_for_footprint<256 * 1024>          == Tier::L2Resident);
static_assert(required_tier_for_footprint<256 * 1024 + 1>      == Tier::L3Resident);
static_assert(required_tier_for_footprint<16 * 1024 * 1024>    == Tier::L3Resident);
static_assert(required_tier_for_footprint<32 * 1024 * 1024>    == Tier::DRAMBound);

// substrate_required_tier_v: TOTAL-storage-driven (matches
// StorageFitsCtxResidency).
static_assert(substrate_required_tier_v<SmallSpsc>    == Tier::L1Resident);
static_assert(substrate_required_tier_v<BoundarySpsc> == Tier::L2Resident);
static_assert(substrate_required_tier_v<LargeSpsc>    == Tier::L3Resident);
static_assert(substrate_required_tier_v<SnapT>        == Tier::L1Resident);

// substrate_hot_path_required_tier_v: per-call-WS-driven.  All
// "normal" cell-size primitives stay L1Resident regardless of N.
static_assert(substrate_hot_path_required_tier_v<SmallSpsc>    == Tier::L1Resident);
static_assert(substrate_hot_path_required_tier_v<BoundarySpsc> == Tier::L1Resident);
static_assert(substrate_hot_path_required_tier_v<LargeSpsc>    == Tier::L1Resident);
static_assert(substrate_hot_path_required_tier_v<SnapT>        == Tier::L1Resident);
// BigCellSpsc (64 KB cell) needs L2.
static_assert(substrate_hot_path_required_tier_v<BigCellSpsc>  == Tier::L2Resident);

}  // namespace detail::substrate_ctx_fit_self_test

}  // namespace crucible::concurrent
