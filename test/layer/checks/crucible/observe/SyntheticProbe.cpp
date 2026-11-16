// The compile-time checks of crucible/observe/SyntheticProbe.h.

#include <crucible/observe/SyntheticProbe.h>
#include <foundation/reflect/EnumName.h>

namespace crucible::observe {

namespace detail {

// The stats grid and the metric ids index a kind by the position of its bit.
// Each kind is one bit, and that bit is at the position of the kind in
// all_transport_probe_kinds, so no index is outside the grid.  O(kinds).
[[nodiscard]] consteval bool each_probe_bit_is_its_position() noexcept {
    std::size_t position = 0;
    for (TransportProbeKind const kind : all_transport_probe_kinds) {
        if (!::fixy::has_single_bit(static_cast<std::uint32_t>(kind)) || transport_probe_index(kind) != position) {
            return false;
        }
        ++position;
    }
    return true;
}

static_assert(each_probe_bit_is_its_position(),
              "Each TransportProbeKind must be one bit at the position of its entry in "
              "all_transport_probe_kinds, because the stats grid and the metric ids use that position.");

// The offset of the last lane of the last kind of the last peer below the
// bound is below 2^32, so the ids of the peers below the bound do not wrap
// into each other.  A lane is one digit of the offset, and the count of lanes
// is the count of the enumerators of the lane.
static_assert(kMaxProbeMetricPeers * all_transport_probe_kinds.size() * kProbeMetricLaneCount
                  <= (std::size_t{1} << 32u),
              "the offset of each peer below kMaxProbeMetricPeers fits 32 bits");
static_assert(kProbeMetricLaneCount == ::foundation::reflect::enum_count<ProbeMetricLane>);
static_assert(metric_id_for(0, 1, TransportProbeKind::RdmaWrite, ProbeMetricLane::LatencyNs)
                  == all_transport_probe_kinds.size() * kProbeMetricLaneCount,
              "the ids of peer 1 start after each lane of each kind of peer 0");

static_assert(alignof(AtomicProbeStats) >= 64, "AtomicProbeStats must be cache-line-aligned so that adjacent "
                                               "(peer, transport_kind) slots in the SyntheticProbeRunner "
                                               "stats_ grid land on distinct lines under concurrent recording");
static_assert(sizeof(AtomicProbeStats) >= 64, "AtomicProbeStats occupies a full cache line. The trailing padding "
                                              "is intentional.");

// Each counter is an Atomic of the base, which accepts only a value with
// lock-free atomic operations, so no recorded outcome takes a lock.
static_assert(::fixy::AtomicCount<std::uint64_t> && ::fixy::AtomicValue<SyntheticProbeFailureClass>);

}  // namespace detail

static_assert(std::is_base_of_v<::foundation::Pinned<SyntheticProbeRunner<1>>, SyntheticProbeRunner<1>>);
static_assert(!CtxFitsSyntheticProbeMint<::fixy::BgDrainCtx>);
static_assert(CtxFitsSyntheticProbeMint<::fixy::ColdInitCtx>);
static_assert(!CtxFitsSyntheticProbeRecord<::fixy::HotFgCtx>);
static_assert(CtxFitsSyntheticProbeRecord<::fixy::BgDrainCtx>);

}  // namespace crucible::observe
