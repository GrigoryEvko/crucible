// The compile-time checks of crucible/observe/Metrics.h.

#include <crucible/observe/Metrics.h>

namespace crucible::observe {

static_assert(std::is_trivially_copyable_v<RuntimeMetrics>);
static_assert(std::is_trivially_destructible_v<RuntimeMetrics>);
static_assert(::fixy::concurrent::SnapshotValue<RuntimeMetricsSample>);

namespace detail::metrics_self_test {

struct probe_reader_brand {};
struct probe_writer_brand {};
using Keeper = KeeperMetricsReader<probe_reader_brand, probe_writer_brand>;
using Canopy = CanopyMetricsReader<probe_reader_brand, probe_writer_brand>;

// The whole point of the split: neither role converts to the other, so
// the two mint names below differ in what they hand back.
static_assert(!std::is_same_v<Keeper, Canopy>);
static_assert(!std::is_convertible_v<Keeper, Canopy>);
static_assert(!std::is_convertible_v<Canopy, Keeper>);
static_assert(sizeof(Keeper) == sizeof(RuntimeMetricsReader<probe_reader_brand, probe_writer_brand>),
              "the role is a type-level marker; it must not cost a byte.");

}  // namespace detail::metrics_self_test

}  // namespace crucible::observe
