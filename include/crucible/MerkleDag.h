#pragma once

#include <crucible/CKernelId.h>
#include <crucible/DagArena.h>
#include <crucible/DimHash.h>
#include <crucible/NumericalRecipe.h>
#include <crucible/StorageNbytes.h>
#include <crucible/TensorMeta.h>
#include <crucible/Types.h>
#include <fixy/Bands.h>
#include <fixy/Core.h>
#include <fixy/Mutation.h>
#include <fixy/Protocol.h>
#include <fixy/Refined.h>
#include <fixy/Saturated.h>
#include <fixy/handle/PublishOnce.h>
#include <foundation/Platform.h>
#include <foundation/Transfer.h>
#include <foundation/contracts/Decide.h>
#include <foundation/contracts/Post.h>
#include <foundation/contracts/Pre.h>
#include <foundation/effects/Effect.h>
#include <foundation/reflect/Mix.h>

#include <array>
#include <bit>
#include <cstdlib>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>

namespace crucible {

struct CompiledKernel;

// The transfer facts of a compiled kernel, which no header defines yet (the
// opaque types of foundation/Transfer.h).  The file that defines
// CompiledKernel must assert, beside the definition, that the definition
// holds the statement:
//
//     static_assert(::foundation::transfer::holds_its_statement(^^CompiledKernel));
//
// A test of the check file of foundation/Transfer.h pins that a definition
// whose walk refuses its statement fails that assertion.
namespace transfer_facts {
// A compiled kernel does not change after its compile, so two threads can
// read one at the same time.
inline constexpr ::foundation::transfer::opaque_facts compiled_kernel{.type = ^^CompiledKernel, .is_shareable = true};
inline constexpr ::foundation::transfer::opaque_seal seal{.count = 1};
}  // namespace transfer_facts

// One slot of a memory plan.  The byte codec of the Protocol family writes
// its 40 bytes in the image of a region: each field in declaration order,
// with each pad byte zero.
struct[[= ::fixy::derive_protocol{}]] TensorSlot {
    uint64_t offset_bytes = 0;
    uint64_t nbytes = 0;
    OpIndex birth_op;
    OpIndex death_op;
    ScalarType dtype = ScalarType::Undefined;
    DeviceType device_type = DeviceType::CPU;
    int8_t device_idx = -1;
    Layout layout = Layout::Strided;
    bool is_external = false;
    [[= ::fixy::pad_part{}]] ::foundation::core::FixedArray<uint8_t, 3> pad{};
    SlotId slot_id;
    [[= ::fixy::pad_part{}]] ::foundation::core::FixedArray<uint8_t, 4> pad2{};
};

// External slots keep the allocations they already have: they are counted in
// num_external and are not placed in the pool.  The slots are in the arena of
// the region, and the plan only borrows them as a run of the DAG arena.
struct MemoryPlan {
    ::fixy::ArenaRun<TensorSlot const, DagArena> slots{};
    uint64_t pool_bytes = 0;
    uint32_t num_external = 0;

    DeviceType device_type = DeviceType::CPU;
    int8_t device_idx = -1;
    ::foundation::core::FixedArray<uint8_t, 2> pad0{};
    uint64_t device_capability = 0;

    int32_t rank = -1;  // -1 when not distributed
    int32_t world_size = 0;  // 0 when not distributed

    // The count of slots.  The pool allocator takes at most kMaxNumSlots
    // slots, so a count that does not fit 32 bits ends the process.
    [[nodiscard]] uint32_t num_slots() const noexcept {
        const size_t count = slots.size();
        CRUCIBLE_PRE(count <= ::fixy::max_of<uint32_t>);
        return static_cast<uint32_t>(count);
    }
};

// A link to the memory plan of a region, or no plan.  The plan is in a DAG
// arena, and the link only borrows it with the brand of the arena.  A region
// only reads its plan, so the link refers to a const plan.  The empty link is
// the null pointer, so a link takes eight bytes.
using PlanLink = ::fixy::Option<::fixy::Ref<MemoryPlan const, DagArena>>;

// A full link to the plan that a DAG arena holds.
[[nodiscard]] inline PlanLink make_plan_link(::fixy::Ref<MemoryPlan const, DagArena> plan) noexcept {
    return ::fixy::some(plan);
}

// The link to the plan that a planner or a loader made, or no plan.
[[nodiscard]] inline PlanLink make_plan_link(::fixy::Option<::fixy::Ref<MemoryPlan, DagArena>> const& plan) noexcept {
    return plan.match([](::fixy::Ref<MemoryPlan, DagArena> const& made) noexcept { return make_plan_link(made); },
                      []() noexcept { return PlanLink{}; });
}

// True when the byte ranges of the slots live at op `op` are pairwise
// disjoint.  Only the simultaneously-live set is checked: two slots whose
// live ranges do not intersect may share an offset, which is the reuse the
// planner exists to produce.  External slots keep their own allocations, so
// their offset_bytes carries no pool meaning and they are skipped.
// A live set larger than MaxLive, or an interval whose endpoint sum
// overflows, returns false — neither case can support a disjointness claim.
template <std::size_t MaxLive>
[[nodiscard]] constexpr bool live_intervals_disjoint_at(::fixy::View<const TensorSlot> slots, OpIndex op) noexcept {
    using SlotBytes = ::foundation::decide::Interval<std::uint64_t>;
    ::fixy::FixedList<SlotBytes, MaxLive> live{};
    const std::uint32_t t = op.raw();
    for (TensorSlot const& s : slots) {
        if (s.is_external) continue;
        const std::uint32_t birth = s.birth_op.raw();
        const std::uint32_t death = s.death_op.raw();
        if (birth <= t && t <= death) {
            if (!::foundation::decide::no_overflow_sum(s.offset_bytes, s.nbytes)) return false;
            const bool is_full = live.append(SlotBytes{.lo = s.offset_bytes, .hi = s.offset_bytes + s.nbytes}).is_err();
            if (is_full) return false;
        }
    }
    return ::foundation::decide::intervals_pairwise_disjoint(live.view());
}

// The storage span is the sum of the per-dimension extents, not the largest
// of them: sizes [3,4] with strides [4,1] span (2*4)+(3*1)+1 = 12 elements.
// A negative stride puts an extent below the base pointer, so the minimum
// and maximum offsets accumulate separately.
//
// Any overflow in the chain saturates to UINT64_MAX with the clamped flag
// set, so a caller cannot mistake a saturated result for a real byte count
// of 2^64-1.
//
// The algorithm itself lives in StorageNbytes.h, once, and this is the
// public name for it. It used to live in both places: a transcription of the
// same twelve overflow-checked steps stood here, and it was the copy the
// runtime ran, while the differential fuzzer that pins scalar against vector
// held the two copies over there. A divergence in the production copy was
// unreportable by construction. Forwarding is what places this name on the
// side of the comparison that has callers.
//
// What stays here is the precondition, which the reference deliberately does
// not carry: the reference is also the fallback the vector routine takes
// after its own screen, and re-checking ndim on that path would charge the
// check twice for a value the caller already vouched for.
[[nodiscard]] constexpr ::fixy::Saturated<uint64_t> compute_storage_nbytes(ExternalTensorMeta meta) {
    CRUCIBLE_PRE(::foundation::decide::in_range<std::uint8_t>(meta.value().ndim, std::uint8_t{0}, std::uint8_t{8}));
    return detail::compute_storage_nbytes_scalar(meta);
}

[[nodiscard]] constexpr ::fixy::DetSafe<::fixy::DetSafeTier_v::Pure, ::fixy::Saturated<uint64_t>>
compute_storage_nbytes_det(ExternalTensorMeta meta) {
    return ::fixy::mint_band<::fixy::DetSafe<::fixy::DetSafeTier_v::Pure, ::fixy::Saturated<uint64_t>>>(
        compute_storage_nbytes(meta));
}

// The record of one op.  Its arrays are runs of a DAG arena, and the entry
// only borrows them: each door takes a run of the DagArena brand.  Three
// counts give their lengths: the inputs, the outputs and the scalar
// arguments.  One door sets each count together with each array of that
// count, so each array holds its count, and no array is absent at a count
// above zero.
struct TraceEntry {
    SchemaHash schema_hash;
    ShapeHash shape_hash;
    ScopeHash scope_hash;
    CallsiteHash callsite_hash;

private:
    // The slot of each count, and the index of each array.  The slot ids
    // come first, because the replay reads them for each op.
    static constexpr std::size_t kInputCount = 0;
    static constexpr std::size_t kOutputCount = 1;
    static constexpr std::size_t kScalarCount = 2;
    static constexpr std::size_t kOutputSlotIds = 0;
    static constexpr std::size_t kInputSlotIds = 1;
    static constexpr std::size_t kInputTraceIndices = 2;
    static constexpr std::size_t kInputMetas = 3;
    static constexpr std::size_t kOutputMetas = 4;
    static constexpr std::size_t kScalarArgs = 5;

    // The members after this one take the two bytes of pad at its end, so a
    // TraceEntry stays 96 bytes with the tags of its scalars.
    //
    // An entry only reads its arrays, so each run holds const elements.
    [[no_unique_address]] ::fixy::RunSet<
        uint16_t, ::fixy::RunOf<SlotId const, kOutputCount>, ::fixy::RunOf<SlotId const, kInputCount>,
        ::fixy::RunOf<OpIndex const, kInputCount>, ::fixy::RunOf<TensorMeta const, kInputCount>,
        ::fixy::RunOf<TensorMeta const, kOutputCount>, ::fixy::RunOf<int64_t const, kScalarCount>> arrays_{};

public:
    bool grad_enabled = false;
    bool inference_mode = false;

    // Stays OPAQUE until a schema-to-kernel mapping is registered for
    // schema_hash.
    CKernelId kernel_id = CKernelId::OPAQUE;
    bool is_mutable = false;  // in-place or out= op

    TrainingPhase training_phase = TrainingPhase::FORWARD;
    bool torch_function = false;

    // The nesting depth of the op.  A lowering takes only the ops of depth
    // zero, because each op of a larger depth runs inside one of them.
    OpDepth depth{};

    // The tag of each scalar argument (ScalarType2 of TraceRing.h) and the
    // data slots (ScalarRole of Types.h).  The bits of a slot past the count
    // are zero.  The tag tells the integer 1 from the double 4.9e-324, whose
    // bits are the same.
    ScalarSlotKinds scalar_kinds{};

    // The counts.
    [[nodiscard]] uint16_t num_inputs() const noexcept { return arrays_.count<kInputCount>(); }
    [[nodiscard]] uint16_t num_outputs() const noexcept { return arrays_.count<kOutputCount>(); }
    [[nodiscard]] uint16_t num_scalar_args() const noexcept { return arrays_.count<kScalarCount>(); }

    // The arrays.  Each array holds the count of its kind.
    [[nodiscard]] ::fixy::View<const TensorMeta> input_metas() const noexcept { return arrays_.view<kInputMetas>(); }
    [[nodiscard]] ::fixy::View<const TensorMeta> output_metas() const noexcept { return arrays_.view<kOutputMetas>(); }
    [[nodiscard]] ::fixy::View<const int64_t> scalar_args() const noexcept { return arrays_.view<kScalarArgs>(); }
    // The producer op of each input.
    [[nodiscard]] ::fixy::View<const OpIndex> input_trace_indices() const noexcept {
        return arrays_.view<kInputTraceIndices>();
    }
    [[nodiscard]] ::fixy::View<const SlotId> input_slot_ids() const noexcept { return arrays_.view<kInputSlotIds>(); }
    [[nodiscard]] ::fixy::View<const SlotId> output_slot_ids() const noexcept { return arrays_.view<kOutputSlotIds>(); }

    // The inputs: the metadata, the producer op and the slot of each input,
    // each a run of a DAG arena.  The three runs hold one count, or the
    // process ends.  An op with no inputs passes three empty runs.
    void set_inputs(::fixy::ArenaRun<TensorMeta const, DagArena> metas,
                    ::fixy::ArenaRun<OpIndex const, DagArena> producers,
                    ::fixy::ArenaRun<SlotId const, DagArena> slot_ids) noexcept {
        arrays_.set_count<kInputCount>(count_of_(metas.size()));
        arrays_.set<kInputMetas>(metas.view());
        arrays_.set<kInputTraceIndices>(producers.view());
        arrays_.set<kInputSlotIds>(slot_ids.view());
    }

    // The outputs: the metadata and the slot of each output, each a run of a
    // DAG arena.  The two runs hold one count, or the process ends.
    void set_outputs(::fixy::ArenaRun<TensorMeta const, DagArena> metas,
                     ::fixy::ArenaRun<SlotId const, DagArena> slot_ids) noexcept {
        arrays_.set_count<kOutputCount>(count_of_(metas.size()));
        arrays_.set<kOutputMetas>(metas.view());
        arrays_.set<kOutputSlotIds>(slot_ids.view());
    }

    // The scalar arguments, a run of a DAG arena.
    void set_scalar_args(::fixy::ArenaRun<int64_t const, DagArena> scalars) noexcept {
        arrays_.set_count<kScalarCount>(count_of_(scalars.size()));
        arrays_.set<kScalarArgs>(scalars.view());
    }

private:
    // An entry keeps each count in 16 bits, and a larger count ends the
    // process.
    [[nodiscard]] static uint16_t count_of_(size_t count) noexcept {
        return ::fixy::narrow<uint16_t>(count).expect("an op holds at most 65535 elements of one kind");
    }
};

// The condition a BranchNode checks to select an arm.  The hash of the
// Protocol family folds each field, the pad too, so a Guard that grows a
// field changes each guard hash.  That is a hash-format break: the merkle
// hash of each branch folds the guard hash, and the store keeps it.  The byte
// codec writes its 12 bytes in the image of a branch.
struct[[= ::fixy::derive_protocol{}]] Guard {
    enum class Kind : uint8_t {
        SHAPE_DIM,
        SCALAR_VALUE,
        DTYPE,
        DEVICE,
        OP_SEQUENCE,  // the op at op_index matches the expected schema
    };

    Kind kind = Kind::SHAPE_DIM;
    // The hash folds a FixedArray as its elements, the same as a C array.
    [[= ::fixy::pad_part{}]] ::foundation::core::FixedArray<uint8_t, 3> pad{};
    OpIndex op_index;
    uint16_t arg_index = 0;  // SHAPE_DIM, DTYPE, DEVICE
    uint16_t dim_index = 0;  // SHAPE_DIM
};

enum class TraceNodeKind : uint8_t {
    REGION,
    BRANCH,
    LOOP,
    TERMINAL,
};

// A kind byte recovered from persisted state is untrusted.  A byte that
// names no kind widens to a value that no switch arm matches and that every
// equality test against a real kind falls through, so control flow goes
// silently wrong instead of failing.  The reader of a header reads the kind
// with fixy::enum_from (read_enumerator of crucible/Serialize.h), which
// refuses such a byte.

struct TraceNode;
struct RegionNode;
struct BranchNode;
struct LoopNode;

// A Ref of a node or a plan of the DAG, with the brand of the DAG arenas.
template <class T>
using DagRef = ::fixy::Ref<T, DagArena>;

// A link to one node of the DAG, or no node.  The node is in a DAG arena, and
// the link only borrows it with the brand of the arena.  The empty link is
// the null pointer, so a link takes eight bytes.
using TraceLink = ::fixy::Option<DagRef<TraceNode>>;

// A link to one region of the DAG, or no region, as TraceLink is.  A table of
// the pipeline that holds the regions of many sites, such as the region cache
// or the slot of a publication, holds each region in this form.
using RegionLink = ::fixy::Option<DagRef<RegionNode>>;
using ConstRegionLink = ::fixy::Option<DagRef<RegionNode const>>;

// A link of a node to the node that it continues to or runs.  A const node
// gives the node of each such link only as const, so a reader of a const
// region writes no node that the region reaches.  The link takes eight bytes
// too.
using NodeLink = ::fixy::Link<TraceNode, DagArena>;

// A kind of node of the DAG: TraceNode or a class that derives from it.
template <class Node>
concept DagNodeType = std::is_class_v<Node> && !std::is_const_v<Node>
                   && (std::is_same_v<Node, TraceNode> || std::is_base_of_v<TraceNode, Node>);

template <DagNodeType Node, class... Refused>
    requires(sizeof...(Refused) == 0)
[[nodiscard]] DagRef<Node> mint_dag_node(::foundation::effects::Alloc a, ::fixy::BumpArena<DagArena>& arena) noexcept;

template <DagNodeType Node, class... Refused>
    requires(sizeof...(Refused) == 0)
[[nodiscard]] DagRef<Node> mint_dag_node(::foundation::effects::Alloc a,
                                         ::fixy::Box<::fixy::BumpArena<DagArena>>& arena) noexcept;

// The key of the constructor of a node.  Its constructor is private, and only
// mint_dag_node builds one, so each node lives in a DAG arena.
class DagNodeKey final {
    constexpr DagNodeKey() noexcept {}

    template <DagNodeType Node, class... Refused>
        requires(sizeof...(Refused) == 0)
    friend DagRef<Node> mint_dag_node(::foundation::effects::Alloc a, ::fixy::BumpArena<DagArena>& arena) noexcept;

    template <DagNodeType Node, class... Refused>
        requires(sizeof...(Refused) == 0)
    friend DagRef<Node> mint_dag_node(::foundation::effects::Alloc a,
                                      ::fixy::Box<::fixy::BumpArena<DagArena>>& arena) noexcept;

public:
    DagNodeKey(DagNodeKey const&) = delete("a key lives in the call of mint_dag_node");
    DagNodeKey(DagNodeKey&&) = delete("a key lives in the call of mint_dag_node");
    DagNodeKey& operator=(DagNodeKey const&) = delete("a key lives in the call of mint_dag_node");
    DagNodeKey& operator=(DagNodeKey&&) = delete("a key lives in the call of mint_dag_node");
    // User-provided, because GCC counts a class whose copy and move are all
    // deleted as trivially copyable, and a bit_cast could then forge a key.
    constexpr ~DagNodeKey() {}
};

// Nodes live in a DAG arena and are never freed individually.  The class
// states the brand of the arena family, so to(node) gives a Ref with the
// DagArena brand.  That is sound because only mint_dag_node builds a node, in
// a BumpArena<DagArena>, and no node copies or moves out of its arena.
struct TraceNode {
    using brand_type = DagArena;

    explicit constexpr TraceNode(DagNodeKey const& /*key*/) noexcept {}
    TraceNode(TraceNode const&) = delete("a node lives in its DAG arena: link the node, do not copy it");
    TraceNode(TraceNode&&) = delete("a node lives in its DAG arena: link the node, do not move it");
    TraceNode& operator=(TraceNode const&) = delete("a node lives in its DAG arena: link the node, do not copy it");
    TraceNode& operator=(TraceNode&&) = delete("a node lives in its DAG arena: link the node, do not move it");
    ~TraceNode() = default;

    TraceNodeKind kind{};
    ::foundation::core::FixedArray<uint8_t, 7> pad{};
    // Identity of this node and every descendant.  The field stays a bare
    // MerkleHash because wrapping it would change the layout.
    MerkleHash merkle_hash;
    // The continuation.  A TERMINAL node has none.
    NodeLink next{};

    // A node that has not been through recompute_merkle carries hash 0, and
    // comparing that against a stored hash reports a spurious mismatch.  This
    // accessor makes "the hash is computed" a precondition instead.  It spells
    // the return type out because the alias for it needs MerkleHash complete
    // and so is declared below.
    [[nodiscard]] ::fixy::Refined<::fixy::non_zero, MerkleHash> computed_merkle_hash() const noexcept {
        CRUCIBLE_PRE(::foundation::decide::is_non_zero(merkle_hash));
        return ::fixy::mint_refined<::fixy::non_zero>(merkle_hash);
    }

    // The node as a region, a branch or a loop, or no value when its kind is
    // a different kind.  The doors read the kind, so no walk downcasts a node
    // by hand.  Each door gives a Ref with the DagArena brand, the brand of
    // each link of the DAG.
    [[nodiscard]] ::fixy::Option<DagRef<RegionNode>> as_region() noexcept;
    [[nodiscard]] ::fixy::Option<DagRef<BranchNode>> as_branch() noexcept;
    [[nodiscard]] ::fixy::Option<DagRef<LoopNode>> as_loop() noexcept;
    [[nodiscard]] ::fixy::Option<DagRef<RegionNode const>> as_region() const noexcept;
    [[nodiscard]] ::fixy::Option<DagRef<BranchNode const>> as_branch() const noexcept;
    [[nodiscard]] ::fixy::Option<DagRef<LoopNode const>> as_loop() const noexcept;

    // The node as a region, a branch or a loop, for a walk that read the kind
    // before.  A node of a different kind ends the process in each build.
    // These doors give no Option, so a walk that knows the kind tests the
    // kind once.
    [[nodiscard]] DagRef<RegionNode> expect_region() noexcept;
    [[nodiscard]] DagRef<BranchNode> expect_branch() noexcept;
    [[nodiscard]] DagRef<LoopNode> expect_loop() noexcept;
    [[nodiscard]] DagRef<RegionNode const> expect_region() const noexcept;
    [[nodiscard]] DagRef<BranchNode const> expect_branch() const noexcept;
    [[nodiscard]] DagRef<LoopNode const> expect_loop() const noexcept;
};

// A full link to the node.  Each node lives in a DAG arena, so the Ref of
// the node has the DagArena brand.
[[nodiscard]] inline TraceLink make_link(TraceNode& node) noexcept { return ::fixy::some(::fixy::to(node)); }

// True when the link reaches the node.  The test compares the addresses of
// the two nodes.
[[nodiscard]] inline bool is_link_to(TraceLink const& link, TraceNode const& node) noexcept {
    return link.is_some_and(
        [&node](DagRef<TraceNode> const& linked) noexcept { return ::fixy::same_object(linked, ::fixy::to(node)); });
}

// True when a link of const reaches the node, as is_link_to above.
[[nodiscard]] inline bool is_link_to(::fixy::Option<DagRef<TraceNode const>> const& link,
                                     TraceNode const& node) noexcept {
    return link.is_some_and([&node](DagRef<TraceNode const> const& linked) noexcept {
        return ::fixy::same_object(linked, ::fixy::to(node));
    });
}

// The step of a walk of a chain of the DAG: the continuation of a node, as a
// link with the constness of the link of the node (walk_chain of
// foundation/core/Walk.h).
struct NextNode final {
    [[nodiscard]] TraceLink operator()(DagRef<TraceNode> const& node) const noexcept { return node->next.option(); }
    [[nodiscard]] ::fixy::Option<DagRef<TraceNode const>>
    operator()(DagRef<TraceNode const> const& node) const noexcept {
        return node->next.option();
    }
};

// The nodes of the chain that starts at first, along the continuations, as a
// walk.  The walk reads each link one time.  Complexity: one step for each
// node that the walk reads.
[[nodiscard]] inline auto walk_trace(TraceLink first) noexcept { return ::fixy::walk_chain(first, NextNode{}); }

// The nodes of the chain that starts at first, as const, as walk_trace above.
[[nodiscard]] inline auto walk_trace(::fixy::Option<DagRef<TraceNode const>> first) noexcept {
    return ::fixy::walk_chain(first, NextNode{});
}

// Zero is the "never built" hash.  A TERMINAL node legitimately carries
// MerkleHash{}, but it is a sentinel leaf, never a root: two unbuilt subtrees
// both hashing to zero compare equal, so a zero root is accepted as proof of
// equivalence with anything.  A caller that persists or transmits a root
// takes this witness instead.
using ValidMerkleRoot = ::fixy::Refined<::fixy::non_zero, MerkleHash>;

[[nodiscard, gnu::const]] inline constexpr MerkleHash make_merkle_root(ValidMerkleRoot raw) noexcept {
    return raw.value();
}

// Zero is the "never folded" content hash.  An empty op list produces it by
// design, so the empty region is the one place it is legal.  Everywhere the
// hash is a key it must be non-zero: two unrelated regions that both lack a
// computed hash would otherwise share a cache slot, and a zero mixed into a
// parent merkle hash is a no-op that hides the missing body.
using ValidContentHash = ::fixy::Refined<::fixy::non_zero, ContentHash>;

[[nodiscard, gnu::const]] inline constexpr ContentHash make_content_hash(ValidContentHash raw) noexcept {
    return raw.value();
}

// A compilable sequence of ops.
struct RegionNode : TraceNode {
    explicit constexpr RegionNode(DagNodeKey const& key) noexcept : TraceNode{key} {}

    ContentHash content_hash;
    // The hash of the numerics policy and the eager word that the content
    // hash folds (compute_numerics_hash), or zero for a region that a caller
    // hashed with no policy.  A load of the region folds the ops under it
    // again to check the content hash.
    NumericsHash numerics;
    // One writer publishes the compiled kernel once and every reader
    // acquire-loads it.  A reader only reads the kernel, so the slot holds
    // a kernel of const.  sizeof(PublishOnce<T*>) equals sizeof(atomic<T*>),
    // so the 88-byte layout below holds.
    ::fixy::handle::PublishOnce<CompiledKernel const> compiled;

    // The ops are in a DAG arena, and the node only borrows them as a run of
    // the arena.  A region only reads its ops.
    ::fixy::ArenaRun<TraceEntry const, DagArena> ops{};

    SchemaHash first_op_schema;

    // Wall-clock execution time in milliseconds.  The field stays a bare
    // float because the layout is locked and the persisted form reads these
    // raw bits.  set_measured_ms carries the non-negative, finite invariant.
    float measured_ms = 0.0f;

    // Which compiled variant is active.  Zero means none is selected yet.
    // Variants are registered in increasing order, so a newer variant always
    // carries a larger id and the counter only ever moves forward.  The
    // counter is unbounded rather than capped, because the ceiling is the
    // runtime cache population and not a compile-time table size.
    using VariantCounter = ::fixy::Monotonic<uint32_t>;
    VariantCounter variant_id = ::fixy::mint_monotonic<uint32_t>(0u);

    // The memory plan of the region.  A region has no plan until the
    // liveness analysis runs.
    PlanLink plan{};

    void set_measured_ms(float ms) noexcept {
        CRUCIBLE_PRE(ms >= 0.0f);  // >= also rejects NaN
        CRUCIBLE_PRE(!::fixy::fp::is_inf(ms));
        measured_ms = ms;
    }

    [[nodiscard, gnu::pure]] bool has_measurement() const noexcept { return measured_ms > 0.0f; }

    [[nodiscard, gnu::pure]] bool has_variant() const noexcept { return variant_id.get() != 0u; }

    // The count of ops.  The loader takes at most CDAG_MAX_OPS ops, and a
    // region of the pipeline counts its ops in 32 bits.
    [[nodiscard]] uint32_t num_ops() const noexcept {
        const size_t count = ops.size();
        CRUCIBLE_PRE(count <= ::fixy::max_of<uint32_t>);
        return static_cast<uint32_t>(count);
    }

    // The op at index, which is below num_ops().  An index at or past the
    // count ends the process.
    [[nodiscard]] const TraceEntry& op_at(uint32_t index) const noexcept {
        return *ops.view().get(index).expect("an op index of a region is below its count of ops");
    }

    // The field itself stays bare because the layout is locked.  A region
    // built from zero ops hashes to zero by design, and this accessor
    // refuses that case: those callers read content_hash directly.
    [[nodiscard]] ValidContentHash computed_content_hash() const noexcept {
        CRUCIBLE_PRE(::foundation::decide::is_non_zero(content_hash));
        return ::fixy::mint_refined<::fixy::non_zero>(content_hash);
    }

    // There is no way back to zero.  The non-zero gate sits on this boundary
    // rather than only inside the counter so that a caller passing zero is
    // reported against set_variant.
    void set_variant(uint32_t new_id) noexcept {
        CRUCIBLE_PRE(::foundation::decide::is_non_zero(new_id));
        variant_id.advance(new_id);
        CRUCIBLE_POST(0, variant_id.get() == new_id);
    }
};

// A guard point where execution can diverge.  Arms are kept sorted by value.
struct BranchNode : TraceNode {
    explicit constexpr BranchNode(DagNodeKey const& key) noexcept : TraceNode{key} {}

    Guard guard;

    // One arm: the guard value and the node that the arm runs.  A const arm
    // gives its node only as const.  The arm copies from a const arm, so an
    // arena can fill a run of arms with one arm.
    class Arm {
    public:
        int64_t value = 0;  // the observed guard outcome

        constexpr Arm() noexcept = default;
        constexpr Arm(int64_t guard_value, TraceLink arm_target) noexcept : value{guard_value}, target_{arm_target} {}

        // The node that the arm runs.  An arm of a loaded branch has no
        // target until a caller resolves it.
        [[nodiscard]] TraceLink target() noexcept { return target_; }
        [[nodiscard]] ::fixy::Option<DagRef<TraceNode const>> target() const noexcept {
            return target_.match(
                [](DagRef<TraceNode> const& node) noexcept { return ::fixy::some(DagRef<TraceNode const>{node}); },
                []() noexcept { return ::fixy::Option<DagRef<TraceNode const>>{}; });
        }
        void set_target(TraceLink arm_target) noexcept { target_ = arm_target; }

        // The target of the arm.  A hash of the branch reads the target of
        // each arm, so an arm with no target ends the process here.
        [[nodiscard]] TraceNode& resolved_target() noexcept {
            return *TraceLink{target_}.expect("an arm of the branch has no target: resolve the targets of a "
                                              "loaded branch before a walk reads them");
        }
        [[nodiscard]] TraceNode const& resolved_target() const noexcept {
            return *target().expect("an arm of the branch has no target: resolve the targets of a "
                                    "loaded branch before a walk reads them");
        }

    private:
        TraceLink target_{};
    };

    uint32_t pad1 = 0;  // the bytes between the guard and the arms
    // The inherited next points at the merge node where all arms reconverge.

    // The arms, which are in a DAG arena: the node only borrows them as a run
    // of the arena.  A const branch gives its arms only as const, so a reader
    // of a const branch writes no node that an arm links.
    [[nodiscard]] ::fixy::View<Arm> arms() noexcept { return arms_.view(); }
    [[nodiscard]] ::fixy::View<Arm const> arms() const noexcept { return arms_.view(); }
    void set_arms(::fixy::ArenaRun<Arm, DagArena> arms) noexcept { arms_ = arms; }

    // The count of arms.  The loader takes at most CDAG_MAX_BRANCH_ARMS arms.
    [[nodiscard]] uint32_t num_arms() const noexcept {
        const size_t count = arms_.size();
        CRUCIBLE_PRE(count <= ::fixy::max_of<uint32_t>);
        return static_cast<uint32_t>(count);
    }

    // Sortedness is load-bearing: replay narrows with `arms[mid].value < val`,
    // so unsorted arms route to the wrong target, and that only surfaces much
    // later as a replay divergence against a region that hashes identically.
    // Adjacent-pair comparison admits duplicate values, because two guards
    // that compare equal are not structurally forbidden.
    [[nodiscard, gnu::cold]] bool are_arms_sorted_by_value() const noexcept {
        for (const ::fixy::View<Arm, 2> pair : arms_.view().windows<2>())
            if (pair.at(::fixy::index<1>()).value < pair.at(::fixy::index<0>()).value) return false;
        return true;
    }

private:
    ::fixy::ArenaRun<Arm, DagArena> arms_{};
};

// Carries one of the loop body's outputs back to one of its inputs for the
// next iteration.
struct[[= ::fixy::derive_protocol{}]] FeedbackEdge {
    uint16_t output_idx = 0;
    uint16_t input_idx = 0;

    [[nodiscard]] friend constexpr bool operator==(FeedbackEdge, FeedbackEdge) noexcept = default;
};

// Cyclic computation held inside the acyclic DAG: an acyclic body sub-DAG
// plus feedback edges and a termination condition.  The inherited next
// continues after every iteration has run.
enum class LoopTermKind : uint8_t {
    REPEAT,
    UNTIL,
};

struct LoopNode : TraceNode {
    explicit constexpr LoopNode(DagNodeKey const& key) noexcept : TraceNode{key} {}

    ContentHash body_content_hash;
    // The first node of the body: a chain that ends in TERMINAL.  make_loop
    // sets it, and replay refuses a loop with an empty body.  A const loop
    // gives the body only as const.
    NodeLink body{};
    // The edges are in a DAG arena, and the node only borrows them as a run
    // of the arena.  A loop only reads its edges.
    ::fixy::ArenaRun<FeedbackEdge const, DagArena> feedback_edges{};
    LoopTermKind term_kind = LoopTermKind::REPEAT;
    uint8_t pad_l0 = 0;
    uint16_t pad_l1 = 0;
    uint32_t repeat_count = 0;  // REPEAT: the fixed count.  UNTIL: iterations observed
    float epsilon = 0.0f;  // convergence threshold, UNTIL only
    float measured_body_ms = 0.0f;

    // The count of edges.  make_loop takes a count of 16 bits.
    [[nodiscard]] uint16_t num_feedback() const noexcept {
        const size_t count = feedback_edges.size();
        CRUCIBLE_PRE(count <= ::fixy::max_of<uint16_t>);
        return static_cast<uint16_t>(count);
    }

    // Unlike an empty region, a zero body hash is never legal here: the only
    // factory for a LoopNode folds a non-empty body chain, so zero means the
    // body was never populated.
    [[nodiscard]] ValidContentHash computed_body_content_hash() const noexcept {
        CRUCIBLE_PRE(::foundation::decide::is_non_zero(body_content_hash));
        return ::fixy::mint_refined<::fixy::non_zero>(body_content_hash);
    }
};

// Each node lives in a DAG arena, so to() gives the Ref of the node the
// DagArena brand that each node class states.
inline ::fixy::Option<DagRef<RegionNode>> TraceNode::as_region() noexcept {
    if (kind != TraceNodeKind::REGION) return ::fixy::none;
    return ::fixy::some(::fixy::to(static_cast<RegionNode&>(*this)));
}

inline ::fixy::Option<DagRef<BranchNode>> TraceNode::as_branch() noexcept {
    if (kind != TraceNodeKind::BRANCH) return ::fixy::none;
    return ::fixy::some(::fixy::to(static_cast<BranchNode&>(*this)));
}

inline ::fixy::Option<DagRef<LoopNode>> TraceNode::as_loop() noexcept {
    if (kind != TraceNodeKind::LOOP) return ::fixy::none;
    return ::fixy::some(::fixy::to(static_cast<LoopNode&>(*this)));
}

inline ::fixy::Option<DagRef<RegionNode const>> TraceNode::as_region() const noexcept {
    if (kind != TraceNodeKind::REGION) return ::fixy::none;
    return ::fixy::some(::fixy::to(static_cast<RegionNode const&>(*this)));
}

inline ::fixy::Option<DagRef<BranchNode const>> TraceNode::as_branch() const noexcept {
    if (kind != TraceNodeKind::BRANCH) return ::fixy::none;
    return ::fixy::some(::fixy::to(static_cast<BranchNode const&>(*this)));
}

inline ::fixy::Option<DagRef<LoopNode const>> TraceNode::as_loop() const noexcept {
    if (kind != TraceNodeKind::LOOP) return ::fixy::none;
    return ::fixy::some(::fixy::to(static_cast<LoopNode const&>(*this)));
}

// The check of each door is a fatal invariant and not a precondition.  GCC
// keeps the test of a contract check after a switch on the same kind, and
// it folds the test of a fatal invariant there.  The invariant also tests
// the kind in a unit on the ignore semantic.
inline DagRef<RegionNode> TraceNode::expect_region() noexcept {
    CRUCIBLE_FATAL_INVARIANT(kind == TraceNodeKind::REGION);
    return ::fixy::to(static_cast<RegionNode&>(*this));
}

inline DagRef<BranchNode> TraceNode::expect_branch() noexcept {
    CRUCIBLE_FATAL_INVARIANT(kind == TraceNodeKind::BRANCH);
    return ::fixy::to(static_cast<BranchNode&>(*this));
}

inline DagRef<LoopNode> TraceNode::expect_loop() noexcept {
    CRUCIBLE_FATAL_INVARIANT(kind == TraceNodeKind::LOOP);
    return ::fixy::to(static_cast<LoopNode&>(*this));
}

inline DagRef<RegionNode const> TraceNode::expect_region() const noexcept {
    CRUCIBLE_FATAL_INVARIANT(kind == TraceNodeKind::REGION);
    return ::fixy::to(static_cast<RegionNode const&>(*this));
}

inline DagRef<BranchNode const> TraceNode::expect_branch() const noexcept {
    CRUCIBLE_FATAL_INVARIANT(kind == TraceNodeKind::BRANCH);
    return ::fixy::to(static_cast<BranchNode const&>(*this));
}

inline DagRef<LoopNode const> TraceNode::expect_loop() const noexcept {
    CRUCIBLE_FATAL_INVARIANT(kind == TraceNodeKind::LOOP);
    return ::fixy::to(static_cast<LoopNode const&>(*this));
}

// The hash of the Protocol family of the edges as one run: their images one
// after the other, and then their count, so {A} and {A, A} differ and the
// order of the edges takes part.  An empty edge set hashes to 0, and a
// non-empty set never does: the one fold that gives 0 gives 1 here.
CRUCIBLE_PURE inline uint64_t feedback_signature(::fixy::View<const FeedbackEdge> edges) noexcept {
    if (edges.size() == 0) return 0;
    uint64_t const signature = ::fixy::hash(edges).raw();
    return signature == 0 ? uint64_t{1} : signature;
}

// The local Spec is the explicit projection onto the fields that carry
// termination identity.  Hashing the whole LoopNode instead would double
// count the body and the feedback edges, which are hashed on their own.  The
// hash of the Protocol family reads the epsilon by its bits.
CRUCIBLE_PURE inline uint64_t loopterm_hash(const LoopNode& ln) noexcept {
    struct[[= ::fixy::derive_protocol{}]] Spec {
        LoopTermKind term_kind;
        uint32_t repeat_count;
        float epsilon;
    };
    return ::fixy::hash(Spec{ln.term_kind, ln.repeat_count, ln.epsilon}).raw();
}

// Pure is sound despite the pointer chase: the chain reachable through body
// and the content_hash of each region on it are both fixed at construction.
[[nodiscard, gnu::pure]] inline ContentHash compute_body_content_hash(TraceNode& body) noexcept {
    uint64_t body_hash_state = 0x9E3779B97F4A7C15ULL;
    for (const DagRef<TraceNode> node : walk_trace(make_link(body))) {
        for (const DagRef<RegionNode>& region : node->as_region())
            body_hash_state = ::foundation::reflect::combine_ids(body_hash_state, region->content_hash.raw());
    }
    return ContentHash{::foundation::reflect::fmix64(body_hash_state)};
}

// The word of one tensor in the content hash of a region.  It folds each fact
// of the tensor that changes the elements that an op reads:
//
//   - The XOR fold of its extents and strides (dims, DimHash.h).
//   - Its element type and its device, in the bits 0 to 23.  The device comes
//     through device_of, so the word agrees with the equality of a device:
//     one index for the host.
//   - Its rank, its layout and its negative and conjugate bits, in the bits
//     24 to 47.  A lane of extent zero and stride zero adds nothing to the
//     fold of the dims, so only the rank tells [4, 0] from [4].
//   - Its storage offset, times kStorageOffsetMix of DimHash.h.
//
// A tensor of rank zero, the strided layout, no bit and the offset zero gives
// the word of the format before the rank, the layout, the bits and the offset
// folded.  The guard word of an op folds this word for each input too
// (compute_shape_hash of vessel/torch/entry_slots.h), so the guard and the
// content hash tell two inputs apart in the same way.
//
// The vector routine and the scalar routine of DimHash.h give the same dims
// (test_simd).  The content hash takes the vector routine, and the guard of
// the hot path takes the scalar loop, which reads only the lanes below the
// rank of the input.
//
// The word is part of the content-hash format.  A change of it changes each
// stored content hash and each guard word.
[[nodiscard, gnu::always_inline]] inline uint64_t pack_tensor_word(const TensorMeta& meta,
                                                                   const DimHashDet& dims) noexcept {
    constexpr auto kValueBits = static_cast<uint8_t>(meta_flags::IS_NEG | meta_flags::IS_CONJ);
    const uint64_t per_dim_hash_xor = raw_dim_hash(dims);
    const TensorDevice device = device_of(meta);
    const uint64_t meta_packed = static_cast<uint64_t>(std::to_underlying(meta.dtype))
                               | (static_cast<uint64_t>(std::to_underlying(device.type())) << 8)
                               | (static_cast<uint64_t>(::fixy::bit_cast<uint8_t>(device.index())) << 16)
                               | (uint64_t{meta.ndim} << 24)
                               | (static_cast<uint64_t>(::fixy::bit_cast<uint8_t>(meta.layout)) << 32)
                               | (static_cast<uint64_t>(meta.flags & kValueBits) << 40);
    const uint64_t offset_word = static_cast<uint64_t>(meta.storage_offset) * detail::kStorageOffsetMix;
    return per_dim_hash_xor ^ meta_packed ^ offset_word;
}

// The whole region content-hash format, and the only place it is written.
//
// A region's content hash is the compiler's cache key, so it has to come out
// the same from every producer.  It has three steps — the seed, the per-op
// mix, and the finalizer — and all three are part of the format.  Two
// producers exist: the span fold below, and the streaming fold the
// background thread runs as it builds a region out of the drained ring.  A
// producer that spelled any one of the three steps for itself would move
// independently of the other, and the two hashes for one region would then
// disagree with no build failure to say so.
//
// Hence this object rather than three loose pieces: a producer drives it and
// cannot reach the steps.  Changing a step here changes every producer at
// once, and invalidates every stored content hash — which the frozen vectors
// in test_merkle_dag are there to make loud.
class ContentHashFold {
public:
    // The tag a producer passes to say that it selects no numerics at all,
    // as opposed to having forgotten to pass the recipe it does have.
    //
    // The two are the same 64 bits and always were, because the no-recipe
    // path folds nothing.  What the tag buys is that they are no longer the
    // same source code.  Under the old shape the recipe arrived through an
    // optional method, so a producer that never called it got a
    // recipe-blind cache key and no diagnostic: the whole runtime path did
    // exactly that.  Under this shape omission does not compile, and the
    // producers that genuinely have no recipe say so at the construction
    // site, where a reader looking for the recipe finds the answer.
    //
    // Passing this tag is a claim about the producer, not about the region:
    // "nothing in this build selects a numerical recipe, so every region it
    // hashes is under the same unnamed numerics".  That claim holds today
    // and stops holding the moment a recipe source exists.  The grep that
    // finds every site to revisit then is `NoRecipe`.
    struct NoRecipe {};

    ContentHashFold() = delete("a content hash is a numerics-specific cache key, so a fold has to "
                               "state which NumericalRecipe it covers: pass the recipe, or pass "
                               "ContentHashFold::NoRecipe{} to state that this producer selects none");

    explicit constexpr ContentHashFold(NoRecipe) noexcept {}

    // The recipe folds in before any op so its contribution propagates
    // through every later mix.  Folding it after the ops would barely
    // perturb the result for short op sequences.
    //
    // Folding a recipe makes the hash recipe-specific, so two regions with
    // byte-identical ops but different numerical recipes land in different
    // kernel-cache slots.  Without that, a kernel compiled under one recipe
    // would serve lookups made under another.
    //
    // A recipe hash of zero means the recipe was never interned, and folding
    // zero would produce the same hash as the NoRecipe path — exactly the
    // confusion the parameter exists to prevent.  UINT64_MAX is reserved as
    // the end-of-region marker and can never be a real recipe hash.
    explicit ContentHashFold(const NumericalRecipe& recipe) noexcept {
        CRUCIBLE_PRE(::foundation::decide::is_non_zero(recipe.hash));
        CRUCIBLE_PRE(!recipe.hash.is_sentinel());
        state_ = ::foundation::reflect::combine_ids(state_, recipe.hash.raw());
    }

    // The fold of a region that the background pipeline records: the hash of
    // the numerics policy of the run and of the eager word folds in before
    // any op, as a recipe does.  So two runs of the same ops under two
    // policies, or under two eager words, give two content hashes.  A hash of
    // zero is the hash of no policy, and the precondition refuses it.
    explicit ContentHashFold(NumericsHash numerics) noexcept {
        CRUCIBLE_PRE(::foundation::decide::is_non_zero(numerics));
        CRUCIBLE_PRE(!numerics.is_sentinel());
        state_ = ::foundation::reflect::combine_ids(state_, numerics.raw());
    }

    [[gnu::always_inline]] void fold(const TraceEntry& op_record) noexcept { fold_entry_(state_, op_record); }

    [[nodiscard, gnu::always_inline]] ContentHash finish() const noexcept {
        return ContentHash{::foundation::reflect::fmix64(state_)};
    }

private:
    // The per-entry contribution to a region's content hash.  It is private
    // because a second copy of the fold would drift, and two disagreeing
    // hashes for one region destroy the suffix and kernel sharing that
    // compares them.
    //
    // The fold order is part of the hash format.  Changing the order, or what
    // each step mixes in, invalidates every stored content hash.
    //
    // Every step is the same chain: `state = fmix64(state ^ input)`.  Three
    // different spellings used to live here — a wymix for tensors, an
    // XOR-multiply for scalars, and a bare wymix for the schema hash —
    // because wymix collapsed on a zero second operand and each site worked
    // around it separately.  fmix64 is a permutation and has no absorbing
    // value, so there is nothing left to work around and one spelling covers
    // the whole fold.  See the fmix64 doc-block in foundation/reflect/Mix.h.
    //
    // The count folds unconditionally, before the values, so that N arguments
    // differ from M even when the first min(N, M) agree, and so an op with
    // neither tensors nor scalars still perturbs the accumulator.
    //
    // Per tensor, the dimensions XOR-fold into one value and a single step
    // merges it with the packed metadata.  The alternative, one step per
    // dimension, makes each mix depend on the previous result and cannot
    // pipeline.  The two are XORed into one operand rather than folded in
    // separate steps for that reason; they are disjoint in meaning, and a
    // permutation cannot lose either one.
    //
    // The op folds its depth and its two tensor counts in one step after the
    // schema.  A post-order list does not show its nesting: "B inside A" and
    // "B, then A" are one list, and the depth of B tells them apart.  The
    // counts keep the inputs and the outputs of an op apart.
    //
    // Each input folds its metadata, and then its producer op.  So two regions
    // that read the same tensors in a different order hash apart.  Each
    // output folds its metadata, so the shape of an output that no later op
    // reads is part of the hash too.
    //
    // No slot id folds.  The producer of each input gives the wiring, and a
    // slot id adds only the number that the build gave a storage.  The build
    // keys a storage on the address of its first byte, which the allocator
    // gives, so the hash keeps out each value that the build derives from an
    // address.
    //
    // The arrays of the inputs hold one count (TraceEntry::set_inputs), so
    // one index reads the two.
    [[gnu::always_inline]] static void fold_entry_(uint64_t& content_hash_state, const TraceEntry& op_record) noexcept {
        content_hash_state = ::foundation::reflect::combine_ids(content_hash_state, op_record.schema_hash.raw());
        content_hash_state = ::foundation::reflect::combine_ids(
            content_hash_state, (uint64_t{op_record.depth.raw()} << 32) | (uint64_t{op_record.num_outputs()} << 16)
                                    | uint64_t{op_record.num_inputs()});

        const ::fixy::View<const TensorMeta> input_metas = op_record.input_metas();
        const ::fixy::View<const OpIndex> producers = op_record.input_trace_indices();
        for (std::size_t input = 0; input < input_metas.size(); ++input) {
            const TensorMeta& input_meta =
                *input_metas.get(input).expect("the input arrays of an entry hold one count");
            content_hash_state = ::foundation::reflect::combine_ids(
                content_hash_state, pack_tensor_word(input_meta, detail::dim_hash_simd_det(input_meta)));
            content_hash_state = ::foundation::reflect::combine_ids(
                content_hash_state,
                uint64_t{producers.get(input).expect("the input arrays of an entry hold one count")->raw()});
        }

        for (const TensorMeta& output_meta : op_record.output_metas()) {
            content_hash_state = ::foundation::reflect::combine_ids(
                content_hash_state, pack_tensor_word(output_meta, detail::dim_hash_simd_det(output_meta)));
        }

        // A data slot folds zero for its value, because the computation reads
        // the value at run time (ScalarRole of Types.h).  A slot past the
        // inline slots has no role, and its value folds.
        content_hash_state =
            ::foundation::reflect::combine_ids(content_hash_state, static_cast<uint64_t>(op_record.num_scalar_args()));
        // The mask of a slot is each bit for a structural slot and no bit for
        // a data slot, so the fold has no branch.  The data bits run out
        // after the inline slots.
        uint32_t data_left = op_record.scalar_kinds.data_slots().bits();
        for (const int64_t scalar : op_record.scalar_args()) {
            const uint64_t structural_mask = uint64_t{data_left & 1u} - 1;
            content_hash_state =
                ::foundation::reflect::combine_ids(content_hash_state, static_cast<uint64_t>(scalar) & structural_mask);
            data_left >>= 1;
        }
        // The tags and the data slots, one step for the op.  Only the bits of
        // the slots below the count fold, so bits that no scalar owns cannot
        // move the hash.  At most five slots have a tag.  An op with no data
        // slot folds the word of its tags alone.
        const ScalarCount tagged =
            ScalarCount::check(::fixy::min(std::size_t{op_record.num_scalar_args()}, std::size_t{kScalarSlots}))
                .expect("the minimum is a count of the slots");
        content_hash_state = ::foundation::reflect::combine_ids(content_hash_state,
                                                                uint64_t{op_record.scalar_kinds.first(tagged).bits()});
    }

    // The golden-ratio constant, matching the seed every other fold in this
    // header starts from.  It is deliberately not shared with them: each
    // fold is its own format, and a fold that adopted this name would tie
    // its format to this one.
    static constexpr uint64_t kSeed = 0x9E3779B97F4A7C15ULL;

    uint64_t state_ = kSeed;
};

// The content hash of ops under no numerical recipe.
[[nodiscard, gnu::pure]] inline ContentHash compute_content_hash(::fixy::View<const TraceEntry> ops) noexcept {
    ContentHashFold fold{ContentHashFold::NoRecipe{}};
    for (const TraceEntry& op_record : ops) {
        fold.fold(op_record);
    }
    return fold.finish();
}

// The content hash of ops under the numerics policy of a run, as the
// background pipeline folds it while it builds a region.
[[nodiscard, gnu::pure]] inline ContentHash compute_content_hash(::fixy::View<const TraceEntry> ops,
                                                                 NumericsHash numerics) noexcept {
    CRUCIBLE_PRE(::foundation::decide::is_non_zero(numerics));
    CRUCIBLE_PRE(!numerics.is_sentinel());
    ContentHashFold fold{numerics};
    for (const TraceEntry& op_record : ops) {
        fold.fold(op_record);
    }
    return fold.finish();
}

// The content hash of ops under one numerical recipe.  The recipe is
// borrowed for the call.
[[nodiscard, gnu::pure]] inline ContentHash compute_content_hash(::fixy::View<const TraceEntry> ops,
                                                                 const NumericalRecipe& recipe) noexcept {
    CRUCIBLE_PRE(::foundation::decide::is_non_zero(recipe.hash));
    CRUCIBLE_PRE(!recipe.hash.is_sentinel());
    ContentHashFold fold{recipe};
    for (const TraceEntry& op_record : ops) {
        fold.fold(op_record);
    }
    return fold.finish();
}

// The recipe that a fold covers, or no recipe.  A function that takes it has
// no default for it, so a caller states the choice: ::fixy::none for a
// producer that selects no numerics.  A recipe comes from a pool, with the
// brand of the arena of the pool.
using RecipeChoice = ::fixy::Option<RecipeRef>;

// True for no recipe, and for a recipe whose hash was interned: a hash that
// is not zero and not the end-of-region marker.
[[nodiscard]] constexpr bool is_foldable_recipe(RecipeChoice const& recipe) noexcept {
    return recipe.is_none_or([](RecipeRef chosen) noexcept {
        return ::foundation::decide::is_non_zero(chosen->hash) && !chosen->hash.is_sentinel();
    });
}

// The schema of the first op, or the zero schema for no ops.  A region keeps
// it as first_op_schema.
[[nodiscard, gnu::pure]] inline SchemaHash first_op_schema_of(::fixy::View<const TraceEntry> ops) noexcept {
    for (const TraceEntry& first : ops) {
        return first.schema_hash;
    }
    return SchemaHash{};
}

[[nodiscard, gnu::pure]] inline MerkleHash compute_merkle_hash(TraceNode& node) noexcept {
    uint64_t merkle_hash_state;
    switch (node.kind) {
        case TraceNodeKind::REGION:
            merkle_hash_state = node.expect_region()->content_hash.raw();
            break;
        case TraceNodeKind::BRANCH: {
            const DagRef<BranchNode> branch = node.expect_branch();
            merkle_hash_state = ::foundation::reflect::fmix64(::fixy::hash(branch->guard).raw());
            for (const BranchNode::Arm& arm : branch->arms()) {
                merkle_hash_state ^= ::foundation::reflect::fmix64(arm.resolved_target().merkle_hash.raw());
                merkle_hash_state *= 0x9E3779B97F4A7C15ULL;
            }
            break;
        }
        case TraceNodeKind::LOOP: {
            const DagRef<LoopNode> loop = node.expect_loop();
            // The salts keep a loop from hashing the same as a region with
            // the same content.  Mixing is fmix64 and XOR, which survives a
            // zero feedback or termination component: fmix64 is a permutation
            // and has no absorbing value.
            constexpr uint64_t kLoopSalt = 0x4C4F4F504E4F4445ULL;  // "LOOPNODE"
            constexpr uint64_t kFbSalt = 0x6665656462616B00ULL;  // "feedbak\0"
            merkle_hash_state = ::foundation::reflect::fmix64(loop->body_content_hash.raw() ^ kLoopSalt);
            merkle_hash_state ^=
                ::foundation::reflect::fmix64(feedback_signature(loop->feedback_edges.view()) ^ kFbSalt);
            merkle_hash_state ^= loopterm_hash(*loop);
            break;
        }
        case TraceNodeKind::TERMINAL:
            return MerkleHash{};
        default:
            ::fixy::unreachable();
    }

    for (const DagRef<TraceNode>& continuation : node.next)
        merkle_hash_state = ::foundation::reflect::fmix64(merkle_hash_state ^ continuation->merkle_hash.raw());

    return MerkleHash{merkle_hash_state};
}

// Content hash for one kernel that fuses every arm of a branch.  Pure is
// sound: the guard and the arms' content hashes are fixed at construction.
[[nodiscard, gnu::pure]] inline ContentHash branched_content_hash(BranchNode const& branch) noexcept {
    uint64_t branched_hash_state = ::foundation::reflect::fmix64(::fixy::hash(branch.guard).raw());
    for (const BranchNode::Arm& arm : branch.arms()) {
        for (const DagRef<RegionNode const>& region : arm.resolved_target().as_region()) {
            branched_hash_state ^= ::foundation::reflect::fmix64(region->content_hash.raw());
            branched_hash_state *= 0x9E3779B97F4A7C15ULL;
        }
    }
    return ContentHash{::foundation::reflect::fmix64(branched_hash_state)};
}

// The one door that builds a node of the DAG: a Node with each field at its
// default, in the arena, with the brand of the arena.  Only this door builds
// the key of the constructor of a node, so each node lives in a DAG arena.
template <DagNodeType Node, class... Refused>
    requires(sizeof...(Refused) == 0)
[[nodiscard]] DagRef<Node> mint_dag_node(::foundation::effects::Alloc a,
                                         ::fixy::BumpArena<DagArena>& arena CRUCIBLE_LIFETIMEBOUND) noexcept {
    return arena.make<Node>(a, DagNodeKey{});
}

// mint_dag_node in the arena of a Box, such as the arena that a published
// region keeps beside its node.  The Box holds the arena on the heap, so a
// move of the Box moves no node.
template <DagNodeType Node, class... Refused>
    requires(sizeof...(Refused) == 0)
[[nodiscard]] DagRef<Node> mint_dag_node(::foundation::effects::Alloc a, ::fixy::Box<::fixy::BumpArena<DagArena>>& arena
                                         CRUCIBLE_LIFETIMEBOUND) noexcept {
    return arena->make<Node>(a, DagNodeKey{});
}

// The twins of mint_dag_node.  A temporary arena releases the node at the end
// of the statement.
template <class Node, class... Rest>
void mint_dag_node(::foundation::effects::Alloc, ::fixy::BumpArena<DagArena>&&, Rest&&...) =
    delete("a temporary arena releases the node at the end of the statement. Use an arena that has a name");
template <class Node, class... Rest>
void mint_dag_node(::foundation::effects::Alloc, ::fixy::Box<::fixy::BumpArena<DagArena>>&&, Rest&&...) =
    delete("a temporary Box releases its arena and the node at the end of the statement. Use a Box that has a name");

// The ops are stored, not copied: they are a run of a DAG arena, which lives
// as long as the node.
//
// This overload hashes under no numerical recipe, which the recipe-taking
// overload further down is the counterpart to. The choice is visible in the
// signature the caller picks rather than in an argument the caller can leave
// out: a region hashed under the wrong numerics shares a kernel-cache slot
// with one hashed under the right ones, and the runtime would serve either
// kernel to either caller.
[[nodiscard]] inline DagRef<RegionNode> make_region(::foundation::effects::Alloc a,
                                                    ::fixy::BumpArena<DagArena>& arena CRUCIBLE_LIFETIMEBOUND,
                                                    ::fixy::ArenaRun<TraceEntry const, DagArena> ops) noexcept {
    const DagRef<RegionNode> node = mint_dag_node<RegionNode>(a, arena);
    node->kind = TraceNodeKind::REGION;
    node->ops = ops;
    node->content_hash = compute_content_hash(ops.view());
    node->first_op_schema = first_op_schema_of(ops.view());
    CRUCIBLE_POST(node, node->kind == TraceNodeKind::REGION);
    CRUCIBLE_POST(node, node->ops.size() == ops.size());
    CRUCIBLE_POST(node, ::foundation::decide::implies(ops.size() > 0u, node->content_hash.raw() != 0));
    return node;
}

// For a caller that has already folded the hash while streaming the ops under
// the numerics hash.  The node must come fresh from mint_dag_node.
inline void init_region_node(RegionNode& node, ::fixy::ArenaRun<TraceEntry const, DagArena> ops,
                             ContentHash precomputed_hash, NumericsHash numerics) noexcept {
    CRUCIBLE_PRE(::foundation::decide::is_non_zero(precomputed_hash) || ops.size() == 0);
    // A non-zero hash here means a second construction over a live node.
    CRUCIBLE_PRE(node.content_hash.raw() == 0);
    node.kind = TraceNodeKind::REGION;
    node.ops = ops;
    node.content_hash = precomputed_hash;
    node.numerics = numerics;
    node.first_op_schema = first_op_schema_of(ops.view());
    CRUCIBLE_POST(0, node.kind == TraceNodeKind::REGION);
    CRUCIBLE_POST(0, node.ops.size() == ops.size());
    CRUCIBLE_POST(0, node.content_hash == precomputed_hash);
}

// The recipe is borrowed, and the node does not keep it: it participates in
// the content hash and is then dropped, so a caller that needs to recover the
// recipe later must track it separately.  The overload above covers a region
// with no recipe.
[[nodiscard]] inline DagRef<RegionNode> make_region(::foundation::effects::Alloc a,
                                                    ::fixy::BumpArena<DagArena>& arena CRUCIBLE_LIFETIMEBOUND,
                                                    ::fixy::ArenaRun<TraceEntry const, DagArena> ops,
                                                    const NumericalRecipe& recipe) noexcept {
    CRUCIBLE_PRE(::foundation::decide::is_non_zero(recipe.hash));
    CRUCIBLE_PRE(!recipe.hash.is_sentinel());
    const DagRef<RegionNode> node = mint_dag_node<RegionNode>(a, arena);
    node->kind = TraceNodeKind::REGION;
    node->ops = ops;
    node->content_hash = compute_content_hash(ops.view(), recipe);
    node->first_op_schema = first_op_schema_of(ops.view());
    CRUCIBLE_POST(node, node->kind == TraceNodeKind::REGION);
    CRUCIBLE_POST(node, node->ops.size() == ops.size());
    CRUCIBLE_POST(node, ::foundation::decide::implies(ops.size() > 0u, node->content_hash.raw() != 0));
    return node;
}

// The twin of the two make_region overloads above.  The node is in the
// arena, and a temporary arena releases it at the end of the statement.  The
// pack holds the other arguments, and the twin declares no raw pointer
// parameter.
template <class... Rest>
DagRef<RegionNode> make_region(::foundation::effects::Alloc, ::fixy::BumpArena<DagArena>&&, Rest&&...) =
    delete("a temporary arena releases the node at the end of the statement. Use an arena that has a name");

[[nodiscard]] inline DagRef<TraceNode> make_terminal(::foundation::effects::Alloc a, ::fixy::BumpArena<DagArena>& arena
                                                     CRUCIBLE_LIFETIMEBOUND) noexcept {
    const DagRef<TraceNode> node = mint_dag_node<TraceNode>(a, arena);
    node->kind = TraceNodeKind::TERMINAL;
    CRUCIBLE_POST(node, node->kind == TraceNodeKind::TERMINAL);
    return node;
}

// The twin of make_terminal.  A temporary arena releases the node at the end
// of the statement.
void make_terminal(::foundation::effects::Alloc, ::fixy::BumpArena<DagArena>&&) =
    delete("a temporary arena releases the node at the end of the statement. Use an arena that has a name");

// The body must be a complete sub-DAG ending in TERMINAL, and
// body_content_hash must already be folded from that body's region chain.
[[nodiscard]] inline DagRef<LoopNode>
make_loop(::foundation::effects::Alloc a, ::fixy::BumpArena<DagArena>& arena CRUCIBLE_LIFETIMEBOUND, TraceNode& body,
          ContentHash body_content_hash, ::fixy::ArenaRun<FeedbackEdge const, DagArena> feedback,
          LoopTermKind term_kind, uint32_t repeat_count, float epsilon = 0.0f) noexcept {
    CRUCIBLE_PRE(feedback.size() <= ::fixy::max_of<uint16_t>);
    // A convergence distance is a finite number that cannot be negative, and
    // every bit of epsilon reaches the termination hash through a bit_cast,
    // so two loops that behave identically would otherwise hash differently.
    //
    // Spelled as a positive test rather than as the negation of its
    // complement.  `!(epsilon < 0.0f)` reads as the same predicate and is
    // not: every comparison against a NaN is false, so the negation is true
    // and a NaN epsilon passed.  It then reached the bit_cast, where NaN is
    // not one value but 2^24-2 of them, each hashing to a different
    // termination hash — so two loops that agree on everything, including on
    // having no usable threshold, would land in different cache slots.
    //
    // The finite test rejects the infinities on the same grounds: a threshold
    // of +Inf converges on the first iteration whatever the distance, which
    // is not a threshold, and a threshold of -Inf never converges.  The test
    // is fixy::fp::is_finite of the base.
    //
    // This is the third layer of CLAUDE.md §XII — an anonymous expression —
    // because neither layer above it reaches a float.  There is no Refined
    // predicate over floating point in the tree, and decide::non_negative is
    // constrained to std::integral, deliberately: on an integral type the
    // predicate has no NaN case to get wrong, which is the whole content of
    // this guard.  Lifting it is a one-name change once a second float
    // boundary wants the same test.
    CRUCIBLE_PRE(::fixy::fp::is_finite(epsilon));
    CRUCIBLE_PRE(epsilon >= 0.0f);
    // An UNTIL loop counts the iterations that were observed, and observing
    // convergence means running the body and measuring its output, so the
    // count is at least one.  Zero says the loop never ran, and replay walks
    // this count: a zero would replay the loop by skipping it, silently
    // producing the continuation's inputs from the body's un-run outputs.
    // A wrong answer is worse than a trap.
    //
    // REPEAT keeps zero, where it means what it says: run the body no times
    // and continue.
    CRUCIBLE_PRE(term_kind != LoopTermKind::UNTIL || repeat_count > 0u);
    const DagRef<LoopNode> node = mint_dag_node<LoopNode>(a, arena);
    node->kind = TraceNodeKind::LOOP;
    node->body = make_link(body);
    node->body_content_hash = body_content_hash;
    node->feedback_edges = feedback;
    node->term_kind = term_kind;
    node->repeat_count = repeat_count;
    node->epsilon = epsilon;
    CRUCIBLE_POST(node, node->kind == TraceNodeKind::LOOP);
    CRUCIBLE_POST(node, is_link_to(node->body.option(), body));
    CRUCIBLE_POST(node, node->feedback_edges.size() == feedback.size());
    CRUCIBLE_POST(node, node->term_kind == term_kind);
    CRUCIBLE_POST(node, node->repeat_count == repeat_count);
    return node;
}

// The twin of make_loop above.  The node is in the arena, and a temporary
// arena releases it at the end of the statement.  The pack holds the other
// arguments, and the twin declares no raw pointer parameter.
template <class... Rest>
DagRef<LoopNode> make_loop(::foundation::effects::Alloc, ::fixy::BumpArena<DagArena>&&, Rest&&...) =
    delete("a temporary arena releases the node at the end of the statement. Use an arena that has a name");

// Children are recomputed first, because a node's hash folds in the hashes of
// its arms, its body and its continuation.
inline void recompute_merkle(TraceNode& node) {
    for (const DagRef<BranchNode>& branch : node.as_branch()) {
        for (BranchNode::Arm& arm : branch->arms())
            for (const DagRef<TraceNode>& target : arm.target())
                recompute_merkle(*target);
    }
    for (const DagRef<LoopNode>& loop : node.as_loop()) {
        for (const DagRef<TraceNode>& body : loop->body)
            recompute_merkle(*body);
    }
    for (const DagRef<TraceNode>& continuation : node.next)
        recompute_merkle(*continuation);

    node.merkle_hash = compute_merkle_hash(node);
}

// Writes the region of each node of the chain that starts at first, and of
// the arms and the bodies on it, after the first count entries of out, the
// ViewMut of a frame.  The walk only reads the chain, so a frame body can
// give it a node through a const reference and store no borrow outside its
// frame.  The walk recurses into the arms and the bodies, so it takes out by
// reference.  Returns the count of entries, at most the size of out.
template <class Brand, std::size_t Extent>
[[nodiscard]] inline uint32_t
collect_regions(TraceNode const& first, ::fixy::ViewMut<RegionNode const*, Brand, Extent>& out, uint32_t count = 0) {
    for (const DagRef<TraceNode const> node :
         walk_trace(::fixy::Option<DagRef<TraceNode const>>::some(::fixy::to(first)))) {
        if (count >= out.size()) break;
        switch (node->kind) {
            case TraceNodeKind::REGION: {
                auto element = out.get_mut(count).expect("the count is below the size of out");
                *element = &*node->expect_region();
                ++count;
                break;
            }
            case TraceNodeKind::BRANCH: {
                for (BranchNode::Arm const& arm : node->expect_branch()->arms())
                    for (const DagRef<TraceNode const>& target : arm.target())
                        count = collect_regions(*target, out, count);
                break;
            }
            case TraceNodeKind::LOOP:
                for (const DagRef<TraceNode const>& body : node->expect_loop()->body)
                    count = collect_regions(*body, out, count);
                break;
            case TraceNodeKind::TERMINAL:
                break;  // not a stop condition here: the walk follows next
            default:
                ::fixy::unreachable();
        }
    }
    return count;
}

// Returns the link to the first node of the shared suffix, or an empty link
// when the new ops and the existing continuation share no tail.
//
// The recipe is required rather than defaulted, and ::fixy::none is how a
// caller says it selects no numerics. The comparison below is between a hash
// folded here and a hash folded when the existing region was built, so the
// two have to be folded under the same recipe to be comparable at all. A
// defaulted parameter would let a caller that has a recipe omit it and get a
// silent stream of missed merges, every suffix looking distinct because the
// two sides folded different things.
[[nodiscard]] inline TraceLink find_merge_point(::fixy::View<const TraceEntry> new_ops, TraceLink existing_continuation,
                                                RecipeChoice const& recipe) {
    CRUCIBLE_PRE(is_foldable_recipe(recipe));
    constexpr uint32_t MAX_REGIONS = 1024;
    // The regions at the start of the continuation, up to the first node of
    // a different kind, or up to MAX_REGIONS regions.
    ::fixy::FixedList<DagRef<RegionNode>, MAX_REGIONS> existing_regions{};

    for (const DagRef<TraceNode> node : walk_trace(existing_continuation)) {
        if (node->kind != TraceNodeKind::REGION) break;
        if (existing_regions.append(node->expect_region()).is_err()) break;
    }

    TraceLink merge{};
    uint32_t new_pos = static_cast<uint32_t>(new_ops.size());

    for (const DagRef<RegionNode>& region : existing_regions.reverse()) {
        if (new_pos == 0) break;
        const uint32_t region_ops = region->num_ops();
        if (new_pos < region_ops) break;

        const ::fixy::View<const TraceEntry> tail =
            new_ops.window(new_pos - region_ops, region_ops).expect("the tail of the new ops holds the region");
        const ContentHash new_hash =
            recipe.match([tail](RecipeRef chosen) noexcept { return compute_content_hash(tail, *chosen); },
                         [tail]() noexcept { return compute_content_hash(tail); });

        if (new_hash != region->content_hash) break;
        merge = make_link(*region);
        new_pos -= region_ops;
    }

    return merge;
}

// The node of a link, as const.  A walk that only reads takes it.
[[nodiscard]] inline ::fixy::Option<DagRef<TraceNode const>> read_link(TraceLink const& link) noexcept {
    return link.match(
        [](DagRef<TraceNode> const& node) noexcept { return ::fixy::some(DagRef<TraceNode const>{node}); },
        []() noexcept { return ::fixy::Option<DagRef<TraceNode const>>{}; });
}

// GuardEval is int64_t(const Guard&) and RegionExec is void(RegionNode
// const&).  A replay reproduces a recorded execution, so it only reads the
// DAG.
template <typename GuardEval, typename RegionExec>
[[nodiscard, gnu::flatten]] inline bool replay(TraceNode const& first, GuardEval&& eval_guard,
                                               RegionExec&& exec_region) {
    // The walk goes to the continuation of each node after its case.
    for (const DagRef<TraceNode const> node :
         walk_trace(::fixy::Option<DagRef<TraceNode const>>::some(::fixy::to(first)))) {
        switch (node->kind) {
            case TraceNodeKind::REGION: {
                exec_region(*node->expect_region());
                break;
            }

            case TraceNodeKind::BRANCH: {
                const DagRef<BranchNode const> branch = node->expect_branch();
                int64_t val = eval_guard(branch->guard);

                CRUCIBLE_FATAL_INVARIANT(branch->are_arms_sorted_by_value());

                ::fixy::Option<DagRef<TraceNode const>> arm{};
                {
                    const ::fixy::View<const BranchNode::Arm> arms = branch->arms();
                    uint32_t lo = 0, hi = branch->num_arms();
                    while (lo < hi) {
                        uint32_t mid = lo + (hi - lo) / 2;
                        // mid is in [lo, hi) and hi is the count of arms.
                        const BranchNode::Arm& mid_arm = *arms.get(mid).expect("mid is below the count of arms");
                        if (mid_arm.value < val)
                            lo = mid + 1;
                        else if (mid_arm.value > val)
                            hi = mid;
                        else {
                            arm = mid_arm.target();
                            break;
                        }
                    }
                }

                // An unseen guard value, or an arm with no target: the caller
                // falls back to recording.
                if (arm.is_none()) return false;

                for (const DagRef<TraceNode const>& target : arm)
                    if (!replay(*target, eval_guard, exec_region)) return false;
                break;
            }

            case TraceNodeKind::LOOP: {
                const DagRef<LoopNode const> loop = node->expect_loop();
                // Armed in release. An empty body does not fault: the loop
                // below makes no turn and reports a successful replay of a
                // body that never ran. A wrong answer is worse than a trap,
                // and this is a cold path, so the check stays in every build.
                CRUCIBLE_FATAL_INVARIANT(loop->body.is_some());
                // repeat_count drives both termination kinds, and epsilon
                // drives neither.  That is the DetSafe axiom, not an
                // omission: replay reproduces a recorded execution, and a
                // convergence test re-evaluated here would read the values
                // this run produced rather than the ones the recording saw.
                // Two replays of one trace could then take different numbers
                // of iterations, which is the property replay exists to
                // rule out.  So an UNTIL loop replays the count it observed,
                // and epsilon stays what it is everywhere else in this
                // header: part of the loop's identity, folded into the
                // termination hash so that two loops with different
                // thresholds are different computations and cannot share a
                // compiled kernel.
                //
                // Reading epsilon here is therefore a regression, and the
                // UNTIL loops of the epsilon case of test/test_merkle_loop.cpp
                // report it.
                //
                // An UNTIL count of zero cannot arrive: make_loop rejects it,
                // because it would skip the body and report success.
                CRUCIBLE_FATAL_INVARIANT(loop->term_kind != LoopTermKind::UNTIL || loop->repeat_count > 0u);
                for (uint32_t i = 0; i < loop->repeat_count; i++) {
                    for (const DagRef<TraceNode const>& body : loop->body.option())
                        if (!replay(*body, eval_guard, exec_region)) return false;
                }
                break;
            }

            case TraceNodeKind::TERMINAL:
                return true;

            default:
                ::fixy::unreachable();
        }
    }
    return true;
}

struct DagDiff {
    // The two nodes where the walks diverge.  An empty link is the end of a
    // chain.  The two links are empty when the two DAGs agree.
    TraceLink node_a{};
    TraceLink node_b{};
    uint32_t depth = 0;

    enum class Kind : uint8_t {
        IDENTICAL,
        KIND_MISMATCH,
        CONTENT_MISMATCH,
        BRANCH_MISMATCH,
        LOOP_MISMATCH,
        STRUCTURE_MISMATCH,  // one chain ends while the other continues
    } kind = Kind::IDENTICAL;
};

// Walks two DAGs in lockstep and reports the first divergence.  The walk is
// iterative along the next-chain, so a long trace cannot overflow the stack.
// Only arms and loop bodies recurse, and their depth is bounded.
[[nodiscard]] inline DagDiff dag_diff(TraceLink a, TraceLink b, uint32_t depth = 0) {
    // The diff at the first empty link: IDENTICAL with two empty links when
    // the two chains end together, and STRUCTURE_MISMATCH with the two links
    // when one chain goes on.
    const auto chain_end = [&a, &b, &depth]() noexcept -> DagDiff {
        if (a.is_none() && b.is_none()) return {::fixy::none, ::fixy::none, depth, DagDiff::Kind::IDENTICAL};
        return {a, b, depth, DagDiff::Kind::STRUCTURE_MISMATCH};
    };
    for (;;) {
        CRUCIBLE_LET_ELSE(node_a, TraceLink{a}, chain_end());
        CRUCIBLE_LET_ELSE(node_b, TraceLink{b}, chain_end());
        // Equal hashes mean the whole subtree below is equal.
        if (node_a->merkle_hash == node_b->merkle_hash)
            return {::fixy::none, ::fixy::none, depth, DagDiff::Kind::IDENTICAL};

        if (node_a->kind != node_b->kind) return {a, b, depth, DagDiff::Kind::KIND_MISMATCH};

        if (node_a->kind == TraceNodeKind::TERMINAL)
            return {::fixy::none, ::fixy::none, depth, DagDiff::Kind::IDENTICAL};

        if (node_a->kind == TraceNodeKind::REGION) {
            const DagRef<RegionNode> ra = node_a->expect_region();
            const DagRef<RegionNode> rb = node_b->expect_region();
            if (ra->content_hash != rb->content_hash) return {a, b, depth, DagDiff::Kind::CONTENT_MISMATCH};
            a = node_a->next.option();
            b = node_b->next.option();
            ++depth;
            continue;
        }

        if (node_a->kind == TraceNodeKind::BRANCH) {
            const DagRef<BranchNode> ba = node_a->expect_branch();
            const DagRef<BranchNode> bb = node_b->expect_branch();
            if (!::fixy::equal(ba->guard, bb->guard) || ba->arms().size() != bb->arms().size())
                return {a, b, depth, DagDiff::Kind::BRANCH_MISMATCH};
            for (auto [arm_a, arm_b] :
                 ba->arms().walk().try_zip(bb->arms().walk()).expect("the two branches hold one count of arms")) {
                if (arm_a->value != arm_b->value) return {a, b, depth, DagDiff::Kind::BRANCH_MISMATCH};
                DagDiff arm_diff = dag_diff(arm_a->target(), arm_b->target(), depth + 1);
                if (arm_diff.kind != DagDiff::Kind::IDENTICAL) return arm_diff;
            }
            a = node_a->next.option();
            b = node_b->next.option();
            ++depth;
            continue;
        }

        if (node_a->kind == TraceNodeKind::LOOP) {
            const DagRef<LoopNode> la = node_a->expect_loop();
            const DagRef<LoopNode> lb = node_b->expect_loop();
            if (la->body_content_hash != lb->body_content_hash || la->term_kind != lb->term_kind
                || la->repeat_count != lb->repeat_count
                || ::fixy::bit_cast<uint32_t>(la->epsilon) != ::fixy::bit_cast<uint32_t>(lb->epsilon))
                return {a, b, depth, DagDiff::Kind::LOOP_MISMATCH};
            if (!::fixy::equal(la->feedback_edges.view(), lb->feedback_edges.view()))
                return {a, b, depth, DagDiff::Kind::LOOP_MISMATCH};
            // The bodies agree on content, so compare their structure.
            DagDiff body_diff = dag_diff(la->body.option(), lb->body.option(), depth + 1);
            if (body_diff.kind != DagDiff::Kind::IDENTICAL) return body_diff;
            a = node_a->next.option();
            b = node_b->next.option();
            ++depth;
            continue;
        }

        return {a, b, depth, DagDiff::Kind::KIND_MISMATCH};
    }
}

// The count of the nodes of the chain that starts at the link, with the
// nodes of each arm and each body on it.
[[nodiscard]] inline uint32_t dag_node_count(TraceLink first) {
    uint32_t count = 0;
    for (const DagRef<TraceNode> node : walk_trace(first)) {
        ++count;
        switch (node->kind) {
            case TraceNodeKind::BRANCH: {
                for (BranchNode::Arm& arm : node->expect_branch()->arms())
                    count += dag_node_count(arm.target());
                break;
            }
            case TraceNodeKind::LOOP:
                count += dag_node_count(node->expect_loop()->body.option());
                break;
            case TraceNodeKind::REGION:
            case TraceNodeKind::TERMINAL:
                break;
            default:
                ::fixy::unreachable();
        }
    }
    return count;
}

}  // namespace crucible
