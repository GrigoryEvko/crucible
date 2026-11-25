// The compile-time checks of crucible/MerkleDag.h.

#include <crucible/MerkleDag.h>

#include <foundation/Transfer.h>

#include <type_traits>

namespace crucible {

// A compiled kernel holds no state yet, and its definition holds the
// statement of its transfer facts.
static_assert(std::is_empty_v<CompiledKernel>, "a compiled kernel holds no state until the compute path gives it one");
static_assert(::foundation::transfer::holds_its_statement(^^CompiledKernel),
              "the definition of CompiledKernel holds the statement of its transfer facts");

static_assert(sizeof(TensorSlot) == 40, "TensorSlot must be 40 bytes");
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE_STRICT(TensorSlot);

static_assert(sizeof(MemoryPlan) == 48, "MemoryPlan must be 48 bytes — the on-disk format matches this layout");
// A plan holds a View of its slots, and a View refuses a byte copy, so a plan
// is not trivially copyable.  A plan lives in an arena, which ends no object.
static_assert(std::is_trivially_destructible_v<MemoryPlan>, "an arena ends no MemoryPlan");
static_assert(std::is_standard_layout_v<MemoryPlan>, "MemoryPlan must be standard-layout");

// The arrays of an entry are one ArenaRunSet of six pointers and three counts,
// so the entry takes 96 bytes.  The set refuses a byte copy, as a View does,
// so an entry is not trivially copyable.  An arena ends no entry.
static_assert(sizeof(TraceEntry) == 96, "TraceEntry must be 96 bytes: the replay reads one entry for each op");
static_assert(std::is_trivially_destructible_v<TraceEntry>, "an arena ends no TraceEntry");
static_assert(std::is_nothrow_copy_constructible_v<TraceEntry>, "a TraceEntry copies with no throw");

static_assert(sizeof(Guard) == 12, "Guard must be 12 bytes");
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE_STRICT(Guard);

// A node holds the link to its continuation, and a link refuses a byte copy,
// so a node is not trivially copyable.  An arena ends no node.
static_assert(sizeof(TraceNode) == 24, "TraceNode must be 24 bytes");
static_assert(std::is_trivially_destructible_v<TraceNode>, "an arena ends no TraceNode");

// The link to the plan of a region takes one pointer, so the region keeps
// its 88 bytes.  An arena ends no region.
static_assert(sizeof(PlanLink) == sizeof(void*), "a link to a plan takes one pointer");
static_assert(sizeof(RegionNode) == 88, "RegionNode must be 88 bytes — the persisted layout matches this");
static_assert(std::is_trivially_destructible_v<RegionNode>, "an arena ends no RegionNode");

static_assert(sizeof(BranchNode) == 56, "BranchNode must be 56 bytes — the persisted layout matches this");

static_assert(sizeof(FeedbackEdge) == 4, "FeedbackEdge must be 4 bytes");
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(FeedbackEdge);

// The run of the feedback edges takes 16 bytes, so the node takes 72 and not
// one cache line.  An arena ends no node.
static_assert(sizeof(LoopNode) == 72, "LoopNode must be 72 bytes");
static_assert(std::is_trivially_destructible_v<LoopNode>, "an arena ends no LoopNode");

// Each node lives in a DAG arena: no node has a default constructor, a copy
// or a move, its constructor takes the key that only mint_dag_node builds,
// and the key has no public constructor.  So the class states the DagArena
// brand, and to(node) gives a Ref of that brand.
static_assert(!std::is_default_constructible_v<RegionNode> && !std::is_default_constructible_v<TraceNode>);
static_assert(!std::is_copy_constructible_v<RegionNode> && !std::is_move_constructible_v<RegionNode>);
static_assert(!std::is_copy_constructible_v<BranchNode> && !std::is_copy_constructible_v<LoopNode>);
static_assert(!std::is_copy_assignable_v<TraceNode> && !std::is_move_assignable_v<TraceNode>);
static_assert(!std::is_default_constructible_v<DagNodeKey> && !std::is_copy_constructible_v<DagNodeKey>);
static_assert(!std::is_trivially_copyable_v<DagNodeKey>);
static_assert(std::is_same_v<RegionNode::brand_type, DagArena> && std::is_same_v<LoopNode::brand_type, DagArena>);
static_assert(std::is_same_v<decltype(::fixy::to(std::declval<RegionNode&>())), DagRef<RegionNode>>);
static_assert(DagNodeType<TraceNode> && DagNodeType<RegionNode> && !DagNodeType<RegionNode const>
              && !DagNodeType<MemoryPlan>);

// Each borrow of the DAG carries the DagArena brand, and the runs that a node
// borrows are runs of a DAG arena with the size of a View.
static_assert(std::is_same_v<TraceLink, ::fixy::Option<DagRef<TraceNode>>>);
static_assert(std::is_same_v<NodeLink, ::fixy::Link<TraceNode, DagArena>>);
static_assert(std::is_same_v<decltype(std::declval<RegionNode&>().ops), ::fixy::ArenaRun<TraceEntry const, DagArena>>);
static_assert(sizeof(::fixy::ArenaRun<TraceEntry const, DagArena>) == sizeof(::fixy::View<TraceEntry const>));
static_assert(!std::is_constructible_v<::fixy::ArenaRun<TraceEntry const, DagArena>, ::fixy::View<TraceEntry const>>);

// An arm holds its target as a NodeLink, so a const arm gives its target
// only as const and does not copy.  An arm copies from an arm that is not
// const, and an arena builds a run of arms with build_array.
static_assert(sizeof(BranchNode::Arm) == 16, "an arm holds its value and the one pointer of its link");
static_assert(!std::is_copy_constructible_v<BranchNode::Arm>
              && std::is_constructible_v<BranchNode::Arm, BranchNode::Arm&>);
static_assert(!std::is_copy_assignable_v<BranchNode::Arm>);
static_assert(
    std::is_same_v<decltype(std::declval<BranchNode::Arm const&>().target()), ::fixy::Option<DagRef<TraceNode const>>>);
static_assert(std::is_same_v<decltype(std::declval<BranchNode::Arm const&>().resolved_target()), TraceNode const&>);
static_assert(std::is_same_v<decltype(std::declval<BranchNode::Arm&>().target()), TraceLink>);

// A region of the pipeline does not change after its publication, so two
// threads can read one through a reference to const.  The pin holds each part
// that a reader reaches: the continuation through a const Link, the ops and
// the plan as const, the runs of each op as const, and the kernel as a
// compiled kernel of const, whose statement MerkleDag.h gives.
static_assert(::foundation::transfer::Shareable<RegionNode const>);
static_assert(::foundation::transfer::Shareable<TraceEntry const>
              && ::foundation::transfer::Shareable<MemoryPlan const>);

}  // namespace crucible
