#pragma once

#include <crucible/observe/Observation.h>
#include <crucible/topology/Health.h>
#include <fixy/Core.h>

#include <cstddef>
#include <cstdint>

namespace crucible::observe {

// The slot of a health metric.  The base type has the width of the slot
// field of a packed metric id.
enum class HealthMetricSlot : std::uint8_t {
    Score = 0,
    PhiMilli = 1,
    DropRatePpm = 2,
    WearUsedPpm = 3,
};

// One observation for each slot.
inline constexpr std::size_t kTopologyHealthObservationCount = 4;

using TopologyHealthObservationSet = ::fixy::FixedArray<ObservationSnapshot, kTopologyHealthObservationCount>;
using TopologyHealthObservationBatch = ::fixy::FixedArray<Observation, kTopologyHealthObservationCount>;

// The packed metric id of one slot of one peer: the family TopologyHealth in
// 8 bits, the peer slot in 16 bits, and the metric slot in 8 bits.  Each peer
// slot of the 16 bits has its own ids.
[[nodiscard]] constexpr std::uint32_t topology_health_metric_id(std::uint16_t peer_slot,
                                                                HealthMetricSlot slot) noexcept {
    return pack_metric_id(MetricFamily::TopologyHealth, peer_slot, static_cast<std::uint8_t>(slot));
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
        // Each sink is an AtomicSnapshot, so a reader loads it while the publish writes it.
        sinks.with_element(index, [observation = observations.at(index)](ObservationSnapshot& sink) noexcept {
            record_observation(sink, observation);
        });
    }
}

}  // namespace crucible::observe
