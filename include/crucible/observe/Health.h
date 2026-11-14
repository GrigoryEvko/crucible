#pragma once

#include <crucible/observe/Observation.h>
#include <crucible/topology/Health.h>
#include <fixy/Core.h>

#include <cstddef>
#include <cstdint>

namespace crucible::observe {

enum class HealthMetricSlot : std::uint32_t {
    Score = 0,
    PhiMilli = 1,
    DropRatePpm = 2,
    WearUsedPpm = 3,
};

inline constexpr std::uint32_t kTopologyHealthMetricBase = 0x48450000u;
// One observation for each slot.
inline constexpr std::size_t kTopologyHealthObservationCount = 4;

using TopologyHealthObservationSet =
    ::foundation::core::FixedArray<ObservationSnapshot, kTopologyHealthObservationCount>;
using TopologyHealthObservationBatch = ::foundation::core::FixedArray<Observation, kTopologyHealthObservationCount>;

[[nodiscard]] constexpr std::uint32_t topology_health_metric_id(std::uint16_t peer_slot,
                                                                HealthMetricSlot slot) noexcept {
    return kTopologyHealthMetricBase | (static_cast<std::uint32_t>(peer_slot) << 8u) | static_cast<std::uint32_t>(slot);
}

[[nodiscard]] constexpr TopologyHealthObservationBatch
topology_health_observations(std::uint16_t peer_slot, topology::HealthSnapshot const& snapshot,
                             ObservationSource source = ObservationSource::Runtime) noexcept {
    return TopologyHealthObservationBatch{
        make_observation(ObservationKind::HealthScore, source,
                         topology_health_metric_id(peer_slot, HealthMetricSlot::Score), snapshot.score.raw(),
                         snapshot.sequence),
        make_observation(ObservationKind::PhiMilli, source,
                         topology_health_metric_id(peer_slot, HealthMetricSlot::PhiMilli), snapshot.phi.raw(),
                         snapshot.sequence),
        make_observation(ObservationKind::DropRatePpm, source,
                         topology_health_metric_id(peer_slot, HealthMetricSlot::DropRatePpm), snapshot.drop_rate_ppm,
                         snapshot.sequence),
        make_observation(ObservationKind::WearUsedPpm, source,
                         topology_health_metric_id(peer_slot, HealthMetricSlot::WearUsedPpm), snapshot.wear_used_ppm,
                         snapshot.sequence),
    };
}

inline void publish_topology_health(TopologyHealthObservationSet& sinks, std::uint16_t peer_slot,
                                    topology::HealthSnapshot const& snapshot,
                                    ObservationSource source = ObservationSource::Runtime) noexcept {
    TopologyHealthObservationBatch const observations = topology_health_observations(peer_slot, snapshot, source);
    for (::fixy::Index<kTopologyHealthObservationCount> const index : TopologyHealthObservationSet::indices()) {
        record_observation(sinks.at(index), observations.at(index));
    }
}

}  // namespace crucible::observe
