// The compile-time checks of crucible/TraceGraph.h.

#include <crucible/TraceGraph.h>

namespace crucible {

static_assert(sizeof(Edge) == 12, "Edge must be 12 bytes");
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(Edge);

// A graph holds only runs of its epoch.  An epoch keeps no run of a DAG arena,
// because the body of the epoch can end that arena first.
static_assert(::foundation::core::ArenaKeeps<TraceGraph, TraceGraph::brand_type>);
static_assert(!::foundation::core::ArenaKeeps<::fixy::ArenaRun<TraceEntry const, DagArena>, TraceGraph::brand_type>);
static_assert(!::foundation::core::ArenaKeeps<TraceGraph, DagArena>);

}  // namespace crucible
