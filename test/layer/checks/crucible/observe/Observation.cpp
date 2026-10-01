// The compile-time checks of crucible/observe/Observation.h.

#include <crucible/observe/Observation.h>

namespace crucible::observe {

static_assert(std::is_trivially_copyable_v<Observation>);
static_assert(std::is_trivially_destructible_v<Observation>);

}  // namespace crucible::observe
