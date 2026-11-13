// The compile-time checks of crucible/MerkleDag.h.

#include <crucible/MerkleDag.h>

#include <type_traits>

namespace crucible {

static_assert(sizeof(TensorSlot) == 40, "TensorSlot must be 40 bytes");
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE_STRICT(TensorSlot);

static_assert(sizeof(MemoryPlan) == 48, "MemoryPlan must be 48 bytes — the on-disk format matches this layout");
// A plan holds a View of its slots, and a View refuses a byte copy, so a plan
// is not trivially copyable.  A plan lives in an arena, which ends no object.
static_assert(std::is_trivially_destructible_v<MemoryPlan>, "an arena ends no MemoryPlan");
static_assert(std::is_standard_layout_v<MemoryPlan>, "MemoryPlan must be standard-layout");

// The arrays of an entry are one RunSet of six pointers and three counts, so
// the entry takes 96 bytes.  A RunSet refuses a byte copy, as a View does, so
// an entry is not trivially copyable.  An arena ends no entry.
static_assert(sizeof(TraceEntry) == 96, "TraceEntry must be 96 bytes: the replay reads one entry for each op");
static_assert(std::is_trivially_destructible_v<TraceEntry>, "an arena ends no TraceEntry");
static_assert(std::is_nothrow_copy_constructible_v<TraceEntry>, "a TraceEntry copies with no throw");

static_assert(sizeof(Guard) == 12, "Guard must be 12 bytes");
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE_STRICT(Guard);

static_assert(sizeof(TraceNode) == 24, "TraceNode must be 24 bytes");
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(TraceNode);

static_assert(sizeof(RegionNode) == 80, "RegionNode must be 80 bytes — the persisted layout matches this");

static_assert(sizeof(BranchNode) == 56, "BranchNode must be 56 bytes — the persisted layout matches this");

static_assert(sizeof(FeedbackEdge) == 4, "FeedbackEdge must be 4 bytes");
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(FeedbackEdge);

// The View of the feedback edges takes 16 bytes, so the node takes 72 and not
// one cache line.  An arena ends no node.
static_assert(sizeof(LoopNode) == 72, "LoopNode must be 72 bytes");
static_assert(std::is_trivially_destructible_v<LoopNode>, "an arena ends no LoopNode");

}  // namespace crucible
