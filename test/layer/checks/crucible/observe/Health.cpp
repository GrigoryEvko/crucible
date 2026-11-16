// The compile-time checks of crucible/observe/Health.h.

#include <crucible/observe/Health.h>

#include <meta>
#include <type_traits>

namespace crucible::observe {

static_assert(std::is_trivially_copyable_v<TopologyHealthObservationBatch>);

// Observation i carries slot i. A slot with no observation in the batch
// gets an empty entry with metric id 0, and publish then writes that
// entry to the sink of the slot, so this check refuses it.
static_assert(
    [] {
        TopologyHealthObservationBatch const batch = topology_health_observations(1, topology::HealthSnapshot{});
        for (::fixy::Index<kTopologyHealthObservationCount> const index : batch.indices()) {
            auto const slot = static_cast<HealthMetricSlot>(index.raw());
            if (batch.at(index).metric_id != topology_health_metric_id(1, slot)) {
                return false;
            }
        }
        return true;
    }(),
    "topology_health_observations does not give observation i the metric id of slot i.");

// The id of a health metric takes the peer slot in the 16 bits of the index
// field and the metric slot in the 8 bits of the slot field.
static_assert(
    std::is_same_v<decltype(&topology_health_metric_id), std::uint32_t (*)(std::uint16_t, HealthMetricSlot) noexcept>
        && sizeof(std::uint16_t) * 8 == kMetricIndexBits
        && sizeof(std::underlying_type_t<HealthMetricSlot>) * 8 == kMetricSlotBits,
    "topology_health_metric_id takes each field in a type of the width of the field");
static_assert(topology_health_metric_id(256, HealthMetricSlot::Score)
                  == pack_metric_id(MetricFamily::TopologyHealth, 256, 0),
              "the id of a health metric is the packed id of the family TopologyHealth");

static_assert(kTopologyHealthObservationCount == std::meta::enumerators_of(^^HealthMetricSlot).size(),
              "kTopologyHealthObservationCount is a literal count of the enumerators of HealthMetricSlot, so that "
              "no includer of the header walks the enum.  Write the new count in its initializer.");

}  // namespace crucible::observe
