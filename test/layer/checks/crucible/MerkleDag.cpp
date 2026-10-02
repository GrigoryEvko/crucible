// The compile-time checks of crucible/MerkleDag.h.

#include <crucible/MerkleDag.h>

namespace crucible {

static_assert(sizeof(TensorSlot) == 40, "TensorSlot must be 40 bytes");
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE_STRICT(TensorSlot);

static_assert(sizeof(MemoryPlan) == 48, "MemoryPlan must be 48 bytes — the on-disk format matches this layout");
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE_STRICT(MemoryPlan);

CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(TraceEntry);

static_assert(sizeof(Guard) == 12, "Guard must be 12 bytes");
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE_STRICT(Guard);

static_assert(sizeof(TraceNode) == 24, "TraceNode must be 24 bytes");
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(TraceNode);

static_assert(sizeof(RegionNode) == 80, "RegionNode must be 80 bytes — the persisted layout matches this");

static_assert(sizeof(BranchNode) == 56, "BranchNode must be 56 bytes — the persisted layout matches this");

static_assert(sizeof(FeedbackEdge) == 4, "FeedbackEdge must be 4 bytes");
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(FeedbackEdge);

static_assert(sizeof(LoopNode) == 64, "LoopNode must be 64 bytes (one cache line)");
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(LoopNode);

}  // namespace crucible
