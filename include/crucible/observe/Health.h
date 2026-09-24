#pragma once

#include <crucible/observe/Observation.h>
#include <crucible/topology/Health.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <type_traits>

namespace crucible::observe {

enum class HealthMetricSlot : std::uint32_t {
    Score = 0,
    PhiMilli = 1,
    DropRatePpm = 2,
    WearUsedPpm = 3,
};

inline constexpr std::uint32_t kTopologyHealthMetricBase = 0x48450000u;
// One observation for each slot.
inline constexpr std::size_t kTopologyHealthObservationCount = std::meta::enumerators_of(^^HealthMetricSlot).size();

using TopologyHealthObservationSet = std::array<ObservationSnapshot, kTopologyHealthObservationCount>;
using TopologyHealthObservationBatch = std::array<Observation, kTopologyHealthObservationCount>;

[[nodiscard]] constexpr std::uint32_t topology_health_metric_id(std::uint16_t peer_slot,
                                                                HealthMetricSlot slot) noexcept {
    return kTopologyHealthMetricBase | (static_cast<std::uint32_t>(peer_slot) << 8u) | static_cast<std::uint32_t>(slot);
}

[[nodiscard]] constexpr TopologyHealthObservationBatch
topology_health_observations(std::uint16_t peer_slot, topology::HealthSnapshot const& snapshot,
                             ObservationSource source = ObservationSource::Runtime) noexcept {
    return TopologyHealthObservationBatch{{
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
    }};
}

inline void publish_topology_health(TopologyHealthObservationSet& sinks, std::uint16_t peer_slot,
                                    topology::HealthSnapshot const& snapshot,
                                    ObservationSource source = ObservationSource::Runtime) noexcept {
    TopologyHealthObservationBatch const observations = topology_health_observations(peer_slot, snapshot, source);
    for (std::size_t i = 0; i < observations.size(); ++i) {
        record_observation(sinks[i], observations[i]);
    }
}

static_assert(std::is_trivially_copyable_v<TopologyHealthObservationBatch>);

// Observation i carries slot i. A slot with no observation in the batch
// gets an empty entry with metric id 0, and publish then writes that
// entry to the sink of the slot, so this check refuses it.
static_assert(
    [] {
        TopologyHealthObservationBatch const batch = topology_health_observations(1, topology::HealthSnapshot{});
        for (std::size_t index = 0; index < batch.size(); ++index) {
            auto const slot = static_cast<HealthMetricSlot>(index);
            if (batch[index].metric_id != topology_health_metric_id(1, slot)) {
                return false;
            }
        }
        return true;
    }(),
    "topology_health_observations does not give observation i the metric id of slot i.");

}  // namespace crucible::observe
