#pragma once

// Asks whether a channel fits the residency tier a context claims.  Two
// different footprints answer two different questions, and conflating
// them is the mistake this header exists to prevent.
//
// A ring's producer touches a fixed handful of cache lines per call, no
// matter how large the ring is: the two counters and the one cell it
// writes.  A ring of a million cells therefore has the same hot-path
// footprint as a ring of sixty-four.  Its total storage is another
// matter entirely, and only a caller that touches the whole buffer at
// once cares about that.
//
// So the hot-path gate measures the per-call footprint and is the hard
// one, the one a production function parameterized over a channel and a
// context puts in its requires-clause.  It fires when a single cell is
// large enough to overflow the tier on its own.  The storage gate
// measures the whole buffer and belongs to construction: mapping,
// placement, a residency hint, a scan of everything.  A large ring
// legitimately needs a cold context to build and a hot one to use.
//
// The third question is neither gate.  It reports that the whole
// channel is past the point where sharding starts to pay, and it is a
// signal rather than a refusal: a single producer and consumer over a
// large ring is a perfectly good arrangement.
//
// The cache sizes this header compares against are floors, below every
// supported host's real figures, which the topology reads at runtime.
// Comparing against a floor errs towards "does not fit, move up a
// tier", so this static check refuses more configurations than the
// runtime one would and never fewer.  They are defined in WorkingSet.h,
// which the workload-budget concept also reaches; that header carries
// the rationale for the figures.  A future target below those floors
// means lowering them there and re-auditing the callers.

#include <crucible/concurrent/ExecCtxBridge.h>
#include <crucible/concurrent/ParallelismRule.h>
#include <crucible/concurrent/Substrate.h>
#include <crucible/concurrent/_WorkingSet.h>
#include <crucible/effects/_ExecCtx.h>

#include <cstddef>

namespace crucible::concurrent {

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

// The hot-path gate.  Nearly every pairing passes, because a per-call
// footprint of a few cache lines clears the L1 floor by orders of
// magnitude.  It fires when one cell is so large that it overflows the
// tier by itself.

template <class S, class Ctx>
concept SubstrateFitsCtxResidency = IsSubstrate<S> && ::crucible::effects::IsExecCtx<Ctx>
                                 && fits_in_tier_v<per_call_working_set_v<S>, ctx_residency_tier<Ctx>()>;

// The construction gate.  A context bound to DRAM admits anything, and
// a tighter one admits a moderate channel, but the hottest tier here
// means the entire buffer fits L1 and only a tiny channel does.

template <class S, class Ctx>
concept StorageFitsCtxResidency = IsSubstrate<S> && ::crucible::effects::IsExecCtx<Ctx>
                               && fits_in_tier_v<channel_byte_footprint_v<S>, ctx_residency_tier<Ctx>()>;

// A signal, not a refusal.  One producer and one consumer over a
// channel this size is a valid arrangement, and this only says a
// sharded one is worth considering.

template <class S>
concept SubstrateBenefitsFromParallelism = IsSubstrate<S> && (channel_byte_footprint_v<S> > conservative_l2_per_core);

// The inverse of the construction gate: rather than checking a channel
// against a context that already exists, derive the tier the channel's
// construction context has to claim.

template <std::size_t Footprint>
inline constexpr Tier required_tier_for_footprint = [] consteval {
    if (Footprint <= conservative_l1d_per_core)
        return Tier::L1Resident;
    else if (Footprint <= conservative_l2_per_core)
        return Tier::L2Resident;
    else if (Footprint <= conservative_l3_total)
        return Tier::L3Resident;
    else
        return Tier::DRAMBound;
}();

template <IsSubstrate S>
inline constexpr Tier substrate_required_tier_v = required_tier_for_footprint<channel_byte_footprint_v<S>>;

// The same inverse against the per-call footprint, which lands on the
// hottest tier for anything but an outsized cell.

template <IsSubstrate S>
inline constexpr Tier substrate_hot_path_required_tier_v = required_tier_for_footprint<per_call_working_set_v<S>>;

namespace detail::substrate_ctx_fit_self_test {

namespace eff = ::crucible::effects;

struct UserTag {};

using SmallSpsc = Substrate_t<ChannelTopology::OneToOne, int, 1024, UserTag>;
static_assert(channel_byte_footprint_v<SmallSpsc> == 4 * 1024);
static_assert(fits_in_tier_v<4 * 1024, Tier::L1Resident>);
static_assert(fits_in_tier_v<4 * 1024, Tier::L2Resident>);
static_assert(fits_in_tier_v<4 * 1024, Tier::DRAMBound>);

// Sized to land exactly on the L2 floor.
using BoundarySpsc = Substrate_t<ChannelTopology::OneToOne, int, 65536, UserTag>;
static_assert(channel_byte_footprint_v<BoundarySpsc> == 256 * 1024);
static_assert(!fits_in_tier_v<256 * 1024, Tier::L1Resident>);
static_assert(fits_in_tier_v<256 * 1024, Tier::L2Resident>);

using LargeSpsc = Substrate_t<ChannelTopology::OneToOne, int, 1024 * 1024, UserTag>;
static_assert(channel_byte_footprint_v<LargeSpsc> == 4 * 1024 * 1024);
static_assert(!fits_in_tier_v<4 * 1024 * 1024, Tier::L1Resident>);
static_assert(!fits_in_tier_v<4 * 1024 * 1024, Tier::L2Resident>);
static_assert(fits_in_tier_v<4 * 1024 * 1024, Tier::L3Resident>);

using SnapT = Substrate_t<ChannelTopology::OneToMany_Latest, double, 0, UserTag>;
static_assert(channel_byte_footprint_v<SnapT> == sizeof(double));
static_assert(fits_in_tier_v<sizeof(double), Tier::L1Resident>);

// The point of the hot-path gate: all three sizes clear the hottest
// context, because capacity does not enter the per-call footprint.

static_assert(SubstrateFitsCtxResidency<SmallSpsc, eff::HotFgCtx>);
static_assert(SubstrateFitsCtxResidency<BoundarySpsc, eff::HotFgCtx>);
static_assert(SubstrateFitsCtxResidency<LargeSpsc, eff::HotFgCtx>);

static_assert(SubstrateFitsCtxResidency<SmallSpsc, eff::BgDrainCtx>);
static_assert(SubstrateFitsCtxResidency<BoundarySpsc, eff::BgDrainCtx>);
static_assert(SubstrateFitsCtxResidency<LargeSpsc, eff::BgDrainCtx>);

static_assert(SubstrateFitsCtxResidency<SmallSpsc, eff::ColdInitCtx>);
static_assert(SubstrateFitsCtxResidency<BoundarySpsc, eff::ColdInitCtx>);
static_assert(SubstrateFitsCtxResidency<LargeSpsc, eff::ColdInitCtx>);

static_assert(SubstrateFitsCtxResidency<SnapT, eff::HotFgCtx>);

// And the case where it fires: a cell that overflows L1 on its own,
// which is a pairing that really does cost hot-path latency.

struct alignas(64) Big {
    char buf[64 * 1024];
    auto operator<=>(Big const&) const = default;
};

using BigCellSpsc = Substrate_t<ChannelTopology::OneToOne, Big, 4, UserTag>;
static_assert(per_call_working_set_v<BigCellSpsc> > conservative_l1d_per_core);
static_assert(per_call_working_set_v<BigCellSpsc> <= conservative_l2_per_core);

static_assert(!SubstrateFitsCtxResidency<BigCellSpsc, eff::HotFgCtx>);
static_assert(SubstrateFitsCtxResidency<BigCellSpsc, eff::BgDrainCtx>);
static_assert(SubstrateFitsCtxResidency<BigCellSpsc, eff::ColdInitCtx>);

// The construction gate over the same three, where capacity does
// decide the answer.

static_assert(StorageFitsCtxResidency<SmallSpsc, eff::HotFgCtx>);
static_assert(!StorageFitsCtxResidency<BoundarySpsc, eff::HotFgCtx>);
static_assert(!StorageFitsCtxResidency<LargeSpsc, eff::HotFgCtx>);

static_assert(StorageFitsCtxResidency<SmallSpsc, eff::BgDrainCtx>);
static_assert(StorageFitsCtxResidency<BoundarySpsc, eff::BgDrainCtx>);
static_assert(!StorageFitsCtxResidency<LargeSpsc, eff::BgDrainCtx>);

static_assert(StorageFitsCtxResidency<SmallSpsc, eff::ColdInitCtx>);
static_assert(StorageFitsCtxResidency<BoundarySpsc, eff::ColdInitCtx>);
static_assert(StorageFitsCtxResidency<LargeSpsc, eff::ColdInitCtx>);

static_assert(!SubstrateBenefitsFromParallelism<SmallSpsc>);
// Exactly on the floor, and the comparison is strict.
static_assert(!SubstrateBenefitsFromParallelism<BoundarySpsc>);
static_assert(SubstrateBenefitsFromParallelism<LargeSpsc>);
static_assert(!SubstrateBenefitsFromParallelism<SnapT>);

static_assert(required_tier_for_footprint<sizeof(double)> == Tier::L1Resident);
static_assert(required_tier_for_footprint<32 * 1024> == Tier::L1Resident);
static_assert(required_tier_for_footprint<32 * 1024 + 1> == Tier::L2Resident);
static_assert(required_tier_for_footprint<256 * 1024> == Tier::L2Resident);
static_assert(required_tier_for_footprint<256 * 1024 + 1> == Tier::L3Resident);
// The L3 boundary sits on the floor, not above it.  Naming the floor
// rather than a literal keeps these two cells pinned to it if the
// figure is ever revised.
static_assert(required_tier_for_footprint<conservative_l3_total> == Tier::L3Resident);
static_assert(required_tier_for_footprint<conservative_l3_total + 1> == Tier::DRAMBound);

static_assert(substrate_required_tier_v<SmallSpsc> == Tier::L1Resident);
static_assert(substrate_required_tier_v<BoundarySpsc> == Tier::L2Resident);
static_assert(substrate_required_tier_v<LargeSpsc> == Tier::L3Resident);
static_assert(substrate_required_tier_v<SnapT> == Tier::L1Resident);

// Capacity does not move the per-call answer.
static_assert(substrate_hot_path_required_tier_v<SmallSpsc> == Tier::L1Resident);
static_assert(substrate_hot_path_required_tier_v<BoundarySpsc> == Tier::L1Resident);
static_assert(substrate_hot_path_required_tier_v<LargeSpsc> == Tier::L1Resident);
static_assert(substrate_hot_path_required_tier_v<SnapT> == Tier::L1Resident);
// Only the outsized cell moves it.
static_assert(substrate_hot_path_required_tier_v<BigCellSpsc> == Tier::L2Resident);

}  // namespace detail::substrate_ctx_fit_self_test

}  // namespace crucible::concurrent
