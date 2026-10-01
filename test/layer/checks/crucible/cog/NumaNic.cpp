// The compile-time checks of crucible/cog/NumaNic.h.

#include <crucible/cog/NumaNic.h>

namespace crucible::cog {

static_assert(sizeof(NumaNodeId) == sizeof(std::uint16_t));

}  // namespace crucible::cog
