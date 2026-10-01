// The compile-time checks of crucible/observe/MetricsSample.h.

#include <crucible/observe/MetricsSample.h>

#include <type_traits>

namespace crucible::observe {

static_assert(std::is_trivially_copyable_v<RuntimeMetrics>);
static_assert(std::is_trivially_destructible_v<RuntimeMetrics>);

}  // namespace crucible::observe
