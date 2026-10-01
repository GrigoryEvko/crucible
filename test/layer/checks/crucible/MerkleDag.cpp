// The compile-time checks of crucible/MerkleDag.h.

#include <crucible/MerkleDag.h>

namespace crucible {

static_assert(sizeof(TensorSlot) == 40, "TensorSlot must be 40 bytes");

static_assert(sizeof(MemoryPlan) == 48, "MemoryPlan must be 48 bytes — the on-disk format matches this layout");

static_assert(sizeof(Guard) == 12, "Guard must be 12 bytes");

static_assert(sizeof(TraceNode) == 24, "TraceNode must be 24 bytes");

static_assert(sizeof(RegionNode) == 80, "RegionNode must be 80 bytes — the persisted layout matches this");

static_assert(sizeof(BranchNode) == 56, "BranchNode must be 56 bytes — the persisted layout matches this");

static_assert(sizeof(FeedbackEdge) == 4, "FeedbackEdge must be 4 bytes");

static_assert(sizeof(LoopNode) == 64, "LoopNode must be 64 bytes (one cache line)");

}  // namespace crucible
