// The compile-time checks of crucible/observe/Health.h.

#include <crucible/observe/Health.h>

namespace crucible::observe {

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
