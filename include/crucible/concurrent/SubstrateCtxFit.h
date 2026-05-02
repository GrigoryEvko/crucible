#pragma once

// ── crucible::concurrent::SubstrateCtxFit ───────────────────────────
//
// Cross-tree composition: does a Substrate's working-set footprint
// fit the residency tier claimed by an ExecCtx?  This extends the
// existing ExecCtx heat × resid coherence rule (which catches Heat=
// Hot + Resid=DRAM) one level deeper: a Ctx claiming
// Resid=L1Resident paired with a 1 MB SPSC is the same kind of
// contradiction (an L1 promise with a working set 32× too big for
// the L1d on any real CPU).
//
//   Axiom coverage: TypeSafe — fit check is a numerical comparison
//                   against conservative cache-size bounds at the
//                   type level.  Mismatches surface as a concept
//                   violation at the call site.
//                   InitSafe — pure metafunction; no construction.
//                   DetSafe — consteval; bounds are conservative
//                   compile-time constants.
//   Runtime cost:   zero.
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
//
// Sources:
//   • L1d / L2 per-core: typical x86-64 (Zen 2 onward) and ARM
//     server (Neoverse N2 / Graviton 3).
//   • L3 total: smallest mainstream multi-core x86-64 (Zen 2 4-core
//     32 MB) and ARM server (Neoverse N1 16 MB).  Conservative is
//     ~16 MB — accommodates desktop / 4-channel server SKUs.

#include <crucible/concurrent/ExecCtxBridge.h>         // ctx_residency_tier<Ctx>()
#include <crucible/concurrent/ParallelismRule.h>      // Tier enum
#include <crucible/concurrent/Substrate.h>            // ChannelTopology + extractors
#include <crucible/effects/ExecCtx.h>                 // IsExecCtx + ctx_residency_tier_of_t

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
// The Substrate's channel_byte_footprint must fit in the residency
// tier claimed by the Ctx.  Production functions parameterized over
// (Substrate, Ctx) include this concept as a requires-clause to
// reject incoherent pairings at the call site.

template <class S, class Ctx>
concept SubstrateFitsCtxResidency =
    IsSubstrate<S>
 && ::crucible::effects::IsExecCtx<Ctx>
 && fits_in_tier_v<channel_byte_footprint_v<S>, ctx_residency_tier<Ctx>()>;

// ── Inverse: residency_tier_required_for ────────────────────────────
//
// Given a Substrate's footprint, which is the smallest (most
// permissive) Tier that fits?  Useful when callers want to derive
// the Ctx's required residency from the Substrate rather than
// constraining a pre-existing Ctx.

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

// ── Self-test block ─────────────────────────────────────────────────
namespace detail::substrate_ctx_fit_self_test {

namespace eff = ::crucible::effects;

struct UserTag {};

// SPSC<int, 1024> = 4 KB → fits L1.
using SmallSpsc = Substrate_t<ChannelTopology::OneToOne, int, 1024, UserTag>;
static_assert(channel_byte_footprint_v<SmallSpsc> == 4 * 1024);
static_assert( fits_in_tier_v<4 * 1024, Tier::L1Resident>);
static_assert( fits_in_tier_v<4 * 1024, Tier::L2Resident>);
static_assert( fits_in_tier_v<4 * 1024, Tier::DRAMBound>);

// SPSC<int, 65536> = 256 KB → exactly L2 boundary.
using BoundarySpsc = Substrate_t<ChannelTopology::OneToOne, int, 65536, UserTag>;
static_assert(channel_byte_footprint_v<BoundarySpsc> == 256 * 1024);
static_assert(!fits_in_tier_v<256 * 1024, Tier::L1Resident>);   // > 32 KB
static_assert( fits_in_tier_v<256 * 1024, Tier::L2Resident>);   // == 256 KB

// SPSC<int, 1M> = 4 MB → needs L3 or beyond.
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

// HotFgCtx is L1Resident.  SmallSpsc fits, LargeSpsc does NOT.
static_assert( SubstrateFitsCtxResidency<SmallSpsc,    eff::HotFgCtx>);
static_assert(!SubstrateFitsCtxResidency<BoundarySpsc, eff::HotFgCtx>);
static_assert(!SubstrateFitsCtxResidency<LargeSpsc,    eff::HotFgCtx>);

// BgDrainCtx is L2Resident.  SmallSpsc + BoundarySpsc fit; LargeSpsc doesn't.
static_assert( SubstrateFitsCtxResidency<SmallSpsc,    eff::BgDrainCtx>);
static_assert( SubstrateFitsCtxResidency<BoundarySpsc, eff::BgDrainCtx>);
static_assert(!SubstrateFitsCtxResidency<LargeSpsc,    eff::BgDrainCtx>);

// ColdInitCtx is DRAMBound.  Anything fits.
static_assert(SubstrateFitsCtxResidency<SmallSpsc,    eff::ColdInitCtx>);
static_assert(SubstrateFitsCtxResidency<BoundarySpsc, eff::ColdInitCtx>);
static_assert(SubstrateFitsCtxResidency<LargeSpsc,    eff::ColdInitCtx>);

// ── required_tier_for_footprint inverse mapping ────────────────────

static_assert(required_tier_for_footprint<sizeof(double)>     == Tier::L1Resident);
static_assert(required_tier_for_footprint<32 * 1024>           == Tier::L1Resident);   // boundary
static_assert(required_tier_for_footprint<32 * 1024 + 1>       == Tier::L2Resident);
static_assert(required_tier_for_footprint<256 * 1024>          == Tier::L2Resident);
static_assert(required_tier_for_footprint<256 * 1024 + 1>      == Tier::L3Resident);
static_assert(required_tier_for_footprint<16 * 1024 * 1024>    == Tier::L3Resident);
static_assert(required_tier_for_footprint<32 * 1024 * 1024>    == Tier::DRAMBound);

static_assert(substrate_required_tier_v<SmallSpsc>    == Tier::L1Resident);
static_assert(substrate_required_tier_v<BoundarySpsc> == Tier::L2Resident);
static_assert(substrate_required_tier_v<LargeSpsc>    == Tier::L3Resident);
static_assert(substrate_required_tier_v<SnapT>        == Tier::L1Resident);

}  // namespace detail::substrate_ctx_fit_self_test

// ── Runtime smoke test ──────────────────────────────────────────────

[[gnu::cold]] inline void runtime_smoke_test_substrate_ctx_fit() noexcept {
    namespace eff = ::crucible::effects;
    struct UserTag {};

    // Type-level lookups; runtime exercise to keep concept-based
    // capability checks out from under the static_assert blanket.
    using SmallSpsc = Substrate_t<ChannelTopology::OneToOne, int, 1024, UserTag>;
    using LargeSpsc = Substrate_t<ChannelTopology::OneToOne, int, 1024 * 1024, UserTag>;

    static_assert( SubstrateFitsCtxResidency<SmallSpsc, eff::HotFgCtx>);
    static_assert(!SubstrateFitsCtxResidency<LargeSpsc, eff::HotFgCtx>);
    static_assert( SubstrateFitsCtxResidency<LargeSpsc, eff::ColdInitCtx>);

    // Inverse mapping at runtime.
    [[maybe_unused]] constexpr Tier small_t = substrate_required_tier_v<SmallSpsc>;
    [[maybe_unused]] constexpr Tier large_t = substrate_required_tier_v<LargeSpsc>;
    static_assert(small_t == Tier::L1Resident);
    static_assert(large_t == Tier::L3Resident);
}

}  // namespace crucible::concurrent
