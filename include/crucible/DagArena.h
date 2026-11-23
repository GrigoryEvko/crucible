#pragma once

// The family tag of each arena that holds the DAG: its nodes, the runs that
// the nodes borrow, and the memory plans of its regions.  Each DAG arena is a
// BumpArena<DagArena> (foundation/core/BumpArena.h), and each Ref, Link and
// run of the DAG carries the tag.  So a borrow of a different arena does not
// go into the DAG.  Two arenas of the family share the tag, so a Ref of one
// fits a field that names the other.  That instance identity needs a
// run-time id, or a graph type that takes the brand of its own arena.

#include <foundation/Brand.h>

namespace crucible {

struct[[= ::foundation::brand::arena_family_brand{}]] DagArena final {};

}  // namespace crucible
