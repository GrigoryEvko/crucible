// The compile-time checks of crucible/TraceGraph.h.

#include <crucible/TraceGraph.h>

namespace crucible {

static_assert(sizeof(Edge) == 12, "Edge must be 12 bytes");
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(Edge);

}  // namespace crucible
