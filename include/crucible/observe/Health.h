#pragma once

// Bridge topology health facts onto the observation rail.
//
// topology::Health owns deterministic scoring over supplied telemetry facts.
// observe owns latest-value runtime publication. Keeping the bridge here prevents
// consumers from inventing local metric IDs or a parallel recommendations
// layer for health state.

#include <crucible/observe/Observation.h>
#include <crucible/topology/Health.h>

#include <array>
#include <cstdint>
#include <type_traits>

namespace crucible::observe {

enum class HealthMetricSlot : std::uint32_t {
    Score = 0,
    PhiMilli = 1,
    DropRatePpm = 2,
    WearUsedPpm = 3,
};

inline constexpr std::uint32_t kTopologyHealthMetricBase = 0x4845'0000u;
inline constexpr std::size_t kTopologyHealthObservationCount = 4;

using TopologyHealthObservationSet =
    std::array<ObservationSnapshot, kTopologyHealthObservationCount>;
using TopologyHealthObservationBatch =
    std::array<Observation, kTopologyHealthObservationCount>;

[[nodiscard]] constexpr std::uint32_t
topology_health_metric_id(std::uint16_t peer_slot,
                          HealthMetricSlot slot) noexcept {
    return kTopologyHealthMetricBase
         | (static_cast<std::uint32_t>(peer_slot) << 8u)
         | static_cast<std::uint32_t>(slot);
}

[[nodiscard]] constexpr TopologyHealthObservationBatch
topology_health_observations(
    std::uint16_t peer_slot,
    topology::HealthSnapshot const& snapshot,
    ObservationSource source = ObservationSource::Runtime) noexcept {
    return TopologyHealthObservationBatch{{
        make_observation(
            ObservationKind::HealthScore,
            source,
            topology_health_metric_id(peer_slot, HealthMetricSlot::Score),
            snapshot.score.raw(),
            snapshot.sequence),
        make_observation(
            ObservationKind::PhiMilli,
            source,
            topology_health_metric_id(peer_slot, HealthMetricSlot::PhiMilli),
            snapshot.phi.raw(),
            snapshot.sequence),
        make_observation(
            ObservationKind::DropRatePpm,
            source,
            topology_health_metric_id(peer_slot, HealthMetricSlot::DropRatePpm),
            snapshot.drop_rate_ppm,
            snapshot.sequence),
        make_observation(
            ObservationKind::WearUsedPpm,
            source,
            topology_health_metric_id(peer_slot, HealthMetricSlot::WearUsedPpm),
            snapshot.wear_used_ppm,
            snapshot.sequence),
    }};
}

inline void publish_topology_health(
    TopologyHealthObservationSet& sinks,
    std::uint16_t peer_slot,
    topology::HealthSnapshot const& snapshot,
    ObservationSource source = ObservationSource::Runtime) noexcept {
    TopologyHealthObservationBatch const observations =
        topology_health_observations(peer_slot, snapshot, source);
    for (std::size_t i = 0; i < observations.size(); ++i) {
        record_observation(sinks[i], observations[i]);
    }
}

static_assert(std::is_trivially_copyable_v<TopologyHealthObservationBatch>);
static_assert(kTopologyHealthObservationCount == 4);

}  // namespace crucible::observe
