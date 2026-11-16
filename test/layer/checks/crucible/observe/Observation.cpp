// The compile-time checks of crucible/observe/Observation.h.

#include <crucible/observe/Observation.h>

namespace crucible::observe {

static_assert(std::is_trivially_copyable_v<Observation>);
static_assert(std::is_trivially_destructible_v<Observation>);

// The three fields of a packed metric id fill its 32 bits, and each
// parameter of pack_metric_id has the width of its field.  So no field
// truncates its input, and no field overlaps a different field.
static_assert(kMetricFamilyBits + kMetricIndexBits + kMetricSlotBits == 32 && sizeof(Observation::metric_id) * 8 == 32);
static_assert(sizeof(std::underlying_type_t<MetricFamily>) * 8 == kMetricFamilyBits);
static_assert(
    std::is_same_v<decltype(&pack_metric_id), std::uint32_t (*)(MetricFamily, std::uint16_t, std::uint8_t) noexcept>
        && sizeof(std::uint16_t) * 8 == kMetricIndexBits && sizeof(std::uint8_t) * 8 == kMetricSlotBits,
    "pack_metric_id takes each field in a type of the width of the field");
static_assert(pack_metric_id(MetricFamily::TopologyHealth, 0xFFFF, 0xFF) == 0x48FFFFFFu
                  && pack_metric_id(MetricFamily::Congestion, 0x0100, 0) == 0x43010000u,
              "the family is in bits 24 to 31, the index in bits 8 to 23, and the slot in bits 0 to 7");

}  // namespace crucible::observe
