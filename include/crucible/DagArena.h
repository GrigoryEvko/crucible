#pragma once

// The family tags of the arenas of the DAG and of its compiled kernels.
//
// DagArena is the tag of each arena that holds the DAG: its nodes, the runs
// that the nodes borrow, and the memory plans of its regions.  Each DAG arena
// is a BumpArena<DagArena> (foundation/core/BumpArena.h), and each Ref, Link
// and run of the DAG carries the tag.  Two arenas of the family share the tag,
// so a Ref of one fits a field that names the other.  That instance identity
// needs a run-time id, or a graph type that takes the brand of its own arena.
//
// KernelArena is the tag of each arena that holds compiled kernels.  Each
// kernel is an object of a BumpArena<KernelArena>, and each Ref of a kernel
// carries the tag.  So a region and the kernel cache keep no kernel of a
// different owner.
//
// A region publishes a Ref of its kernel (RegionNode of crucible/MerkleDag.h),
// so DagArena states that each arena of kernels outlives each DAG arena.  A
// borrow of each other arena does not go into the DAG.  No mint proves the
// statement: the code that makes an arena of kernels keeps it until the last
// region that publishes a kernel of it ends.  Kernels that never end, such as
// the objects of an arena in an Immortal, would close the statement, and the
// owner decides that with the content of a compiled kernel and the IR.

#include <foundation/Brand.h>

namespace crucible {

struct[[= ::foundation::brand::arena_family_brand{}]] KernelArena final {};

struct[[= ::foundation::brand::arena_family_brand{}]][[= ::foundation::brand::outlived_by<KernelArena>{}]]
    DagArena final {};

}  // namespace crucible
