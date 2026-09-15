#pragma once

#include <crucible/Arena.h>
#include <crucible/CKernel.h>
#include <crucible/DimHash.h>
#include <crucible/Expr.h>
#include <crucible/IterationDetector.h>
#include <crucible/NumericalRecipe.h>
#include <crucible/Platform.h>
#include <crucible/Reflect.h>
#include <crucible/TensorMeta.h>
#include <crucible/TraceRing.h>
#include <crucible/fixy/Handle.h>
#include <crucible/fixy/Perm.h>
#include <crucible/fixy/Wrap.h>
#include <crucible/safety/Decide.h>
#include <crucible/safety/Post.h>
#include <crucible/safety/Pre.h>

#include <crucible/Types.h>

#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <expected>
#include <new>
#include <span>
#include <utility>

namespace crucible {

struct CompiledKernel;

namespace meta_flags {
inline constexpr uint8_t IS_LEAF = 1 << 0;
inline constexpr uint8_t IS_CONTIGUOUS = 1 << 1;
inline constexpr uint8_t HAS_GRAD_FN = 1 << 2;
inline constexpr uint8_t IS_VIEW = 1 << 3;
inline constexpr uint8_t IS_NEG = 1 << 4;
inline constexpr uint8_t IS_CONJ = 1 << 5;
}  // namespace meta_flags

struct TensorSlot {
    uint64_t offset_bytes = 0;
    // nbytes through pad form a 24-byte block that is copied as one unit.
    // Reordering these members, or moving slot_id in among them, breaks
    // that copy.
    uint64_t nbytes = 0;
    OpIndex birth_op;
    OpIndex death_op;
    ScalarType dtype = ScalarType::Undefined;
    DeviceType device_type = DeviceType::CPU;
    int8_t device_idx = -1;
    Layout layout = Layout::Strided;
    bool is_external = false;
    uint8_t pad[3]{};
    SlotId slot_id;
    uint8_t pad2[4]{};
};

static_assert(sizeof(TensorSlot) == 40, "TensorSlot must be 40 bytes");
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE_STRICT(TensorSlot);

// External slots keep the allocations they already have: they are counted in
// num_external and are not placed in the pool.
struct MemoryPlan {
    TensorSlot* slots = nullptr;
    uint64_t pool_bytes = 0;
    uint32_t num_slots = 0;
    uint32_t num_external = 0;

    DeviceType device_type = DeviceType::CPU;
    int8_t device_idx = -1;
    uint8_t pad0[2]{};
    uint64_t device_capability = 0;

    int32_t rank = -1;  // -1 when not distributed
    int32_t world_size = 0;  // 0 when not distributed
};

static_assert(sizeof(MemoryPlan) == 48, "MemoryPlan must be 48 bytes — the on-disk format matches this layout");
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE_STRICT(MemoryPlan);

// True when the byte ranges of the slots live at op `op` are pairwise
// disjoint.  Only the simultaneously-live set is checked: two slots whose
// live ranges do not intersect may share an offset, which is the reuse the
// planner exists to produce.  External slots keep their own allocations, so
// their offset_bytes carries no pool meaning and they are skipped.
// A live set larger than MaxLive, or an interval whose endpoint sum
// overflows, returns false — neither case can support a disjointness claim.
template <std::size_t MaxLive>
[[nodiscard]] constexpr bool live_intervals_disjoint_at(std::span<const TensorSlot> slots, OpIndex op) noexcept {
    std::array<decide::Interval<std::uint64_t>, MaxLive> live{};
    std::size_t n = 0;
    const std::uint32_t t = op.raw();
    for (TensorSlot const& s : slots) {
        if (s.is_external) continue;
        const std::uint32_t birth = s.birth_op.raw();
        const std::uint32_t death = s.death_op.raw();
        if (birth <= t && t <= death) {
            if (n >= MaxLive) return false;
            if (!decide::no_overflow_sum(s.offset_bytes, s.nbytes)) return false;
            live[n] = {.lo = s.offset_bytes, .hi = s.offset_bytes + s.nbytes};
            ++n;
        }
    }
    return decide::intervals_pairwise_disjoint(std::span<const decide::Interval<std::uint64_t>>(live.data(), n));
}

// The storage span is the sum of the per-dimension extents, not the largest
// of them: sizes [3,4] with strides [4,1] span (2*4)+(3*1)+1 = 12 elements.
// A negative stride puts an extent below the base pointer, so the minimum
// and maximum offsets accumulate separately.
//
// Any overflow in the chain saturates to UINT64_MAX with the clamped flag
// set, so a caller cannot mistake a saturated result for a real byte count
// of 2^64-1.
[[nodiscard]] constexpr fixy::wrap::Saturated<uint64_t> compute_storage_nbytes(ExternalTensorMeta meta)
    pre(::crucible::decide::in_range<std::uint8_t>(meta.value().ndim, std::uint8_t{0}, std::uint8_t{8})) {
    using Sat = fixy::wrap::Saturated<uint64_t>;
    const TensorMeta& raw = meta.value();
    if (raw.ndim == 0) return Sat{element_size(raw.dtype).raw()};
    int64_t max_offset = 0;
    int64_t min_offset = 0;
    for (uint8_t d = 0; d < raw.ndim; d++) {
        const int64_t size = raw_tensor_dim(raw.sizes[d]);
        const int64_t stride = raw_tensor_dim(raw.strides[d]);
        if (size == 0) return Sat{uint64_t{0}};
        int64_t dim_extent_bytes;
        // size is positive here, so `size - 1` cannot overflow.
        if (__builtin_mul_overflow(size - 1, stride, &dim_extent_bytes)) [[unlikely]]
            return Sat{UINT64_MAX, true};
        if (dim_extent_bytes > 0) {
            if (__builtin_add_overflow(max_offset, dim_extent_bytes, &max_offset)) [[unlikely]]
                return Sat{UINT64_MAX, true};
        } else {
            if (__builtin_add_overflow(min_offset, dim_extent_bytes, &min_offset)) [[unlikely]]
                return Sat{UINT64_MAX, true};
        }
    }
    int64_t span_signed;
    if (__builtin_sub_overflow(max_offset, min_offset, &span_signed)) [[unlikely]]
        return Sat{UINT64_MAX, true};
    if (__builtin_add_overflow(span_signed, int64_t{1}, &span_signed)) [[unlikely]]
        return Sat{UINT64_MAX, true};
    // max_offset >= 0 >= min_offset, so the span is non-negative and the
    // cast to uint64_t below preserves it.
    uint64_t total_bytes;
    if (__builtin_mul_overflow(static_cast<uint64_t>(span_signed), static_cast<uint64_t>(element_size(raw.dtype).raw()),
                               &total_bytes)) [[unlikely]]
        return Sat{UINT64_MAX, true};
    return Sat{total_bytes};
}

[[nodiscard]] constexpr fixy::wrap::DetSafe<fixy::wrap::DetSafeTier_v::Pure, fixy::wrap::Saturated<uint64_t>>
compute_storage_nbytes_det(ExternalTensorMeta meta) {
    return fixy::wrap::DetSafe<fixy::wrap::DetSafeTier_v::Pure, fixy::wrap::Saturated<uint64_t>>{
        compute_storage_nbytes(meta)};
}

// Every variable-length array here is arena-allocated and outlives the entry.
struct TraceEntry {
    SchemaHash schema_hash;
    ShapeHash shape_hash;
    ScopeHash scope_hash;
    CallsiteHash callsite_hash;

    TensorMeta* input_metas = nullptr;
    TensorMeta* output_metas = nullptr;
    uint16_t num_inputs = 0;
    uint16_t num_outputs = 0;

    int64_t* scalar_args = nullptr;  // null when num_scalar_args == 0
    uint16_t num_scalar_args = 0;

    bool grad_enabled = false;
    bool inference_mode = false;

    // Stays OPAQUE until a schema-to-kernel mapping is registered for
    // schema_hash.
    CKernelId kernel_id = CKernelId::OPAQUE;
    bool is_mutable = false;  // in-place or out= op

    TrainingPhase training_phase = TrainingPhase::FORWARD;
    bool torch_function = false;

    OpIndex* input_trace_indices = nullptr;  // producer op of each input
    SlotId* input_slot_ids = nullptr;
    SlotId* output_slot_ids = nullptr;

    [[nodiscard]] fixy::wrap::Borrowed<const TensorMeta, TraceEntry> input_span() const CRUCIBLE_LIFETIMEBOUND {
        return input_metas ? fixy::wrap::Borrowed<const TensorMeta, TraceEntry>{input_metas, num_inputs}
                           : fixy::wrap::Borrowed<const TensorMeta, TraceEntry>{};
    }
    [[nodiscard]] fixy::wrap::Borrowed<TensorMeta, TraceEntry> input_span() CRUCIBLE_LIFETIMEBOUND {
        return input_metas ? fixy::wrap::Borrowed<TensorMeta, TraceEntry>{input_metas, num_inputs}
                           : fixy::wrap::Borrowed<TensorMeta, TraceEntry>{};
    }
    [[nodiscard]] fixy::wrap::Borrowed<const TensorMeta, TraceEntry> output_span() const CRUCIBLE_LIFETIMEBOUND {
        return output_metas ? fixy::wrap::Borrowed<const TensorMeta, TraceEntry>{output_metas, num_outputs}
                            : fixy::wrap::Borrowed<const TensorMeta, TraceEntry>{};
    }
    [[nodiscard]] fixy::wrap::Borrowed<TensorMeta, TraceEntry> output_span() CRUCIBLE_LIFETIMEBOUND {
        return output_metas ? fixy::wrap::Borrowed<TensorMeta, TraceEntry>{output_metas, num_outputs}
                            : fixy::wrap::Borrowed<TensorMeta, TraceEntry>{};
    }
    [[nodiscard]] fixy::wrap::Borrowed<const int64_t, TraceEntry> scalar_span() const CRUCIBLE_LIFETIMEBOUND {
        return scalar_args ? fixy::wrap::Borrowed<const int64_t, TraceEntry>{scalar_args, num_scalar_args}
                           : fixy::wrap::Borrowed<const int64_t, TraceEntry>{};
    }
    [[nodiscard]] fixy::wrap::Borrowed<const OpIndex, TraceEntry> trace_index_span() const CRUCIBLE_LIFETIMEBOUND {
        return input_trace_indices ? fixy::wrap::Borrowed<const OpIndex, TraceEntry>{input_trace_indices, num_inputs}
                                   : fixy::wrap::Borrowed<const OpIndex, TraceEntry>{};
    }
    [[nodiscard]] fixy::wrap::Borrowed<const SlotId, TraceEntry> input_slot_span() const CRUCIBLE_LIFETIMEBOUND {
        return input_slot_ids ? fixy::wrap::Borrowed<const SlotId, TraceEntry>{input_slot_ids, num_inputs}
                              : fixy::wrap::Borrowed<const SlotId, TraceEntry>{};
    }
    [[nodiscard]] fixy::wrap::Borrowed<const SlotId, TraceEntry> output_slot_span() const CRUCIBLE_LIFETIMEBOUND {
        return output_slot_ids ? fixy::wrap::Borrowed<const SlotId, TraceEntry>{output_slot_ids, num_outputs}
                               : fixy::wrap::Borrowed<const SlotId, TraceEntry>{};
    }
};

CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(TraceEntry);

// The condition a BranchNode checks to select an arm.
struct Guard {
    enum class Kind : uint8_t {
        SHAPE_DIM,
        SCALAR_VALUE,
        DTYPE,
        DEVICE,
        OP_SEQUENCE,  // the op at op_index matches the expected schema
    };

    Kind kind = Kind::SHAPE_DIM;
    uint8_t pad[3]{};
    OpIndex op_index;
    uint16_t arg_index = 0;  // SHAPE_DIM, DTYPE, DEVICE
    uint16_t dim_index = 0;  // SHAPE_DIM

    // Folds every non-static data member, so a Guard that grows a field
    // changes every guard hash.  That is a hash-format break: stored hashes
    // computed by an earlier layout no longer compare equal.
    CRUCIBLE_PURE uint64_t hash() const noexcept { return crucible::reflect_hash(*this); }
};

static_assert(sizeof(Guard) == 12, "Guard must be 12 bytes");
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE_STRICT(Guard);

enum class TraceNodeKind : uint8_t {
    REGION,
    BRANCH,
    LOOP,
    TERMINAL,
};

// A kind byte recovered from persisted state is untrusted.  A byte past
// TERMINAL widens to an enumerator that no switch arm matches and that every
// equality test against a real kind falls through, so control flow goes
// silently wrong instead of failing.  Constructing this refinement is the
// validation point, and make_trace_node_kind consumes the proof.
using ValidTraceNodeKindRaw = ::crucible::fixy::wrap::Refined<
    ::crucible::fixy::wrap::bounded_above<static_cast<uint8_t>(TraceNodeKind::TERMINAL)>, uint8_t>;

[[nodiscard, gnu::const]] inline constexpr TraceNodeKind make_trace_node_kind(ValidTraceNodeKindRaw raw) noexcept {
    return static_cast<TraceNodeKind>(raw.value());
}

// Nodes are arena-allocated and are never freed individually.
struct TraceNode {
    TraceNodeKind kind{};
    uint8_t pad[7]{};
    // Identity of this node and every descendant.  The field stays a bare
    // MerkleHash because wrapping it would change the layout.
    MerkleHash merkle_hash;
    TraceNode* next = nullptr;  // continuation, null for TERMINAL

    // A node that has not been through recompute_merkle carries hash 0, and
    // comparing that against a stored hash reports a spurious mismatch.  This
    // accessor makes "the hash is computed" a precondition instead.  It spells
    // the return type out because the alias for it needs MerkleHash complete
    // and so is declared below.
    [[nodiscard]] crucible::fixy::wrap::Refined<crucible::fixy::wrap::non_zero, MerkleHash>
    computed_merkle_hash() const noexcept pre(::crucible::decide::is_non_zero(merkle_hash)) {
        return crucible::fixy::wrap::Refined<crucible::fixy::wrap::non_zero, MerkleHash>{merkle_hash};
    }
};

static_assert(sizeof(TraceNode) == 24, "TraceNode must be 24 bytes");
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(TraceNode);

// Zero is the "never built" hash.  A TERMINAL node legitimately carries
// MerkleHash{}, but it is a sentinel leaf, never a root: two unbuilt subtrees
// both hashing to zero compare equal, so a zero root is accepted as proof of
// equivalence with anything.  A caller that persists or transmits a root
// takes this witness instead.
using ValidMerkleRoot = ::crucible::fixy::wrap::Refined<::crucible::fixy::wrap::non_zero, MerkleHash>;

[[nodiscard, gnu::const]] inline constexpr MerkleHash make_merkle_root(ValidMerkleRoot raw) noexcept {
    return raw.value();
}

// Zero is the "never folded" content hash.  An empty op list produces it by
// design, so the empty region is the one place it is legal.  Everywhere the
// hash is a key it must be non-zero: two unrelated regions that both lack a
// computed hash would otherwise share a cache slot, and a zero mixed into a
// parent merkle hash is a no-op that hides the missing body.
using ValidContentHash = ::crucible::fixy::wrap::Refined<::crucible::fixy::wrap::non_zero, ContentHash>;

[[nodiscard, gnu::const]] inline constexpr ContentHash make_content_hash(ValidContentHash raw) noexcept {
    return raw.value();
}

// A compilable sequence of ops.
struct RegionNode : TraceNode {
    ContentHash content_hash;
    // One writer publishes the compiled kernel once and every reader
    // acquire-loads it.  sizeof(PublishOnce<T*>) equals sizeof(atomic<T*>),
    // so the 80-byte layout below holds.
    crucible::fixy::handle::PublishOnce<CompiledKernel> compiled;

    TraceEntry* ops = nullptr;
    uint32_t num_ops = 0;

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
    using VariantCounter = ::crucible::fixy::wrap::Monotonic<uint32_t>;
    VariantCounter variant_id{0u};

    MemoryPlan* plan = nullptr;  // null until liveness analysis runs

    void set_measured_ms(float ms) noexcept pre(ms >= 0.0f)  // >= also rejects NaN
        pre(!std::isinf(ms)) {
        measured_ms = ms;
    }

    [[nodiscard, gnu::pure]] bool has_measurement() const noexcept { return measured_ms > 0.0f; }

    [[nodiscard, gnu::pure]] bool has_variant() const noexcept { return variant_id.get() != 0u; }

    // The field itself stays bare because the layout is locked.  A region
    // built from zero ops hashes to zero by design, and this accessor
    // refuses that case: those callers read content_hash directly.
    [[nodiscard]] ValidContentHash computed_content_hash() const noexcept
        pre(::crucible::decide::is_non_zero(content_hash)) {
        return ValidContentHash{content_hash};
    }

    // There is no way back to zero.  The non-zero gate sits on this boundary
    // rather than only inside the counter so that a caller passing zero is
    // reported against set_variant.
    void set_variant(uint32_t new_id) noexcept pre(::crucible::decide::is_non_zero(new_id)) {
        variant_id.advance(new_id);
        CRUCIBLE_POST(0, variant_id.get() == new_id);
    }
};

static_assert(sizeof(RegionNode) == 80, "RegionNode must be 80 bytes — the persisted layout matches this");

// A guard point where execution can diverge.  Arms are kept sorted by value.
struct BranchNode : TraceNode {
    Guard guard;

    struct Arm {
        int64_t value = 0;  // the observed guard outcome
        TraceNode* target = nullptr;
    };

    Arm* arms = nullptr;
    uint32_t num_arms = 0;
    uint32_t pad1 = 0;
    // The inherited next points at the merge node where all arms reconverge.

    // Sortedness is load-bearing: replay narrows with `arms[mid].value < val`,
    // so unsorted arms route to the wrong target, and that only surfaces much
    // later as a replay divergence against a region that hashes identically.
    // Adjacent-pair comparison admits duplicate values, because two guards
    // that compare equal are not structurally forbidden.
    [[nodiscard, gnu::cold]] bool are_arms_sorted_by_value() const noexcept {
        for (uint32_t i = 1; i < num_arms; ++i)
            if (arms[i].value < arms[i - 1].value) return false;
        return true;
    }
};

static_assert(sizeof(BranchNode) == 56, "BranchNode must be 56 bytes — the persisted layout matches this");

// Carries one of the loop body's outputs back to one of its inputs for the
// next iteration.
struct FeedbackEdge {
    uint16_t output_idx = 0;
    uint16_t input_idx = 0;
};

static_assert(sizeof(FeedbackEdge) == 4, "FeedbackEdge must be 4 bytes");
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(FeedbackEdge);

// Cyclic computation held inside the acyclic DAG: an acyclic body sub-DAG
// plus feedback edges and a termination condition.  The inherited next
// continues after every iteration has run.
enum class LoopTermKind : uint8_t {
    REPEAT,
    UNTIL,
};

struct LoopNode : TraceNode {
    ContentHash body_content_hash;
    TraceNode* body = nullptr;  // self-contained chain ending in TERMINAL
    FeedbackEdge* feedback_edges = nullptr;
    uint16_t num_feedback = 0;
    LoopTermKind term_kind = LoopTermKind::REPEAT;
    uint8_t pad_l0 = 0;
    uint32_t repeat_count = 0;  // REPEAT: the fixed count.  UNTIL: iterations observed
    float epsilon = 0.0f;  // convergence threshold, UNTIL only
    float measured_body_ms = 0.0f;

    [[nodiscard]] std::span<const FeedbackEdge> feedback_span() const CRUCIBLE_LIFETIMEBOUND {
        return feedback_edges ? std::span{feedback_edges, num_feedback} : std::span<const FeedbackEdge>{};
    }

    // Unlike an empty region, a zero body hash is never legal here: the only
    // factory for a LoopNode folds a non-empty body chain, so zero means the
    // body was never populated.
    [[nodiscard]] ValidContentHash computed_body_content_hash() const noexcept
        pre(::crucible::decide::is_non_zero(body_content_hash)) {
        return ValidContentHash{body_content_hash};
    }
};

static_assert(sizeof(LoopNode) == 64, "LoopNode must be 64 bytes (one cache line)");
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(LoopNode);

// An empty edge set hashes to 0 and any non-empty set hashes non-zero.  The fold
// uses fmix64 with a non-zero seed rather than wymix, because wymix(0, 0) is
// 0 and would collapse the chain on a leading {0, 0} edge.  The edge count
// folds in as well, so {A} and {A, A} differ.
CRUCIBLE_PURE inline uint64_t feedback_signature(std::span<const FeedbackEdge> edges) noexcept {
    if (edges.empty()) return 0;
    constexpr uint64_t kSeed = 0x6665656462616B73ULL;  // "feedbaks"
    uint64_t signature_state = kSeed;
    for (const auto& edge : edges) {
        signature_state = detail::fmix64(signature_state ^ reflect_hash(edge));
    }
    signature_state = detail::fmix64(signature_state ^ edges.size());
    return signature_state;
}

// The local Spec is the explicit projection onto the fields that carry
// termination identity.  Hashing the whole LoopNode instead would double
// count the body and the feedback edges, which are hashed on their own.
CRUCIBLE_PURE inline uint64_t loopterm_hash(const LoopNode& ln) noexcept {
    struct Spec {
        LoopTermKind term_kind;
        uint32_t repeat_count;
        float epsilon;
    };
    constexpr uint64_t kSeed = 0x7465726D696E6174ULL;  // "terminat"
    return reflect_fmix_fold<kSeed>(Spec{ln.term_kind, ln.repeat_count, ln.epsilon});
}

// Pure is sound despite the pointer chase: the chain reachable through body
// and the content_hash of each region on it are both fixed at construction.
[[nodiscard, gnu::pure]] inline ContentHash compute_body_content_hash(TraceNode* body) noexcept {
    uint64_t body_hash_state = 0x9E3779B97F4A7C15ULL;
    TraceNode* walk = body;
    while (walk) {
        if (walk->kind == TraceNodeKind::REGION)
            body_hash_state = detail::wymix(body_hash_state, static_cast<RegionNode*>(walk)->content_hash.raw());
        walk = walk->next;
    }
    return ContentHash{detail::fmix64(body_hash_state)};
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
    constexpr ContentHashFold() noexcept = default;

    // The recipe folds in before any op so its contribution propagates
    // through every later mix.  Folding it after the ops would barely
    // perturb the result for short op sequences.
    //
    // A non-null recipe makes the hash recipe-specific, so two regions with
    // byte-identical ops but different numerical recipes land in different
    // kernel-cache slots.  Without that, a kernel compiled under one recipe
    // would serve lookups made under another.
    //
    // A recipe hash of zero means the recipe was never interned, and folding
    // zero would produce the same hash as the no-recipe path — exactly the
    // confusion the parameter exists to prevent.  UINT64_MAX is reserved as
    // the end-of-region marker and can never be a real recipe hash.
    [[gnu::always_inline]] void fold_recipe(const NumericalRecipe* recipe) noexcept
        pre(recipe == nullptr || ::crucible::decide::is_non_zero(recipe->hash))
            pre(recipe == nullptr || !recipe->hash.is_sentinel()) {
        if (recipe == nullptr) return;
        [[assume(recipe->hash.raw() != 0)]];
        [[assume(recipe->hash.raw() != UINT64_MAX)]];
        state_ = detail::wymix(state_, recipe->hash.raw());
    }

    [[gnu::always_inline]] void fold(const TraceEntry& op_record) noexcept { fold_entry_(state_, op_record); }

    [[nodiscard, gnu::always_inline]] ContentHash finish() const noexcept {
        return ContentHash{detail::fmix64(state_)};
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
    // The scalar count folds with an XOR-multiply rather than wymix, because
    // wymix(X, 0) is 0 and a scalar-free op legitimately has a count of 0: that
    // would zero the accumulator and produce a content hash of 0, which is the
    // empty-slot sentinel.  The count folds unconditionally, before the values,
    // so that N arguments differ from M even when the first min(N, M) agree, and
    // so an op with neither tensors nor scalars still perturbs the accumulator.
    //
    // Per tensor, the dimensions XOR-fold into one value and a single wymix
    // merges it.  The alternative, one wymix per dimension, makes each multiply
    // depend on the previous result and cannot pipeline.
    //
    // KNOWN DEFECT: the per-tensor wymix has no such guard, and both of
    // wymix's absorbing values for its second operand are reachable from
    // ordinary metadata, which erases the region prefix.  dtype Undefined is
    // int8_t(-1) and sign-extends the pack to all ones; a uint8 tensor on
    // CPU device 0 packs to zero.  test_merkle_dag pins both cases and
    // spells out the mechanism and the fix.  Repairing it changes the hash
    // format, so it is its own commit.
    //
    // Every producer sizes the scalar_args allocation to num_scalar_args, so the
    // loop over it is in bounds.  A null scalar_args contributes the count only.
    [[gnu::always_inline]] static void fold_entry_(uint64_t& content_hash_state, const TraceEntry& op_record) noexcept {
        content_hash_state = detail::wymix(content_hash_state, op_record.schema_hash.raw());

        for (uint16_t j = 0; j < op_record.num_inputs; j++) {
            const TensorMeta& input_meta = op_record.input_metas[j];
            const uint64_t per_dim_hash_xor = raw_dim_hash(detail::dim_hash_simd_det(input_meta));
            uint64_t meta_packed = static_cast<uint64_t>(std::to_underlying(input_meta.dtype))
                                 | (static_cast<uint64_t>(std::to_underlying(input_meta.device_type)) << 8)
                                 | (static_cast<uint64_t>(static_cast<uint8_t>(input_meta.device_idx)) << 16);
            content_hash_state = detail::wymix(content_hash_state ^ per_dim_hash_xor, meta_packed);
        }

        content_hash_state ^= static_cast<uint64_t>(op_record.num_scalar_args);
        content_hash_state *= 0x100000001b3ULL;
        if (op_record.scalar_args) {
            for (uint16_t s = 0; s < op_record.num_scalar_args; s++) {
                content_hash_state ^= static_cast<uint64_t>(op_record.scalar_args[s]);
                content_hash_state *= 0x100000001b3ULL;
            }
        }
    }

    // The golden-ratio constant, matching the seed every other fold in this
    // header starts from.  It is deliberately not shared with them: each
    // fold is its own format, and a fold that adopted this name would tie
    // its format to this one.
    static constexpr uint64_t kSeed = 0x9E3779B97F4A7C15ULL;

    uint64_t state_ = kSeed;
};

[[nodiscard, gnu::pure]] inline ContentHash compute_content_hash(std::span<const TraceEntry> ops,
                                                                 const NumericalRecipe* recipe = nullptr) noexcept
    pre(recipe == nullptr || ::crucible::decide::is_non_zero(recipe->hash))
        pre(recipe == nullptr || !recipe->hash.is_sentinel()) {
    ContentHashFold fold;
    fold.fold_recipe(recipe);
    for (const auto& op_record : ops) {
        fold.fold(op_record);
    }
    return fold.finish();
}

[[nodiscard, gnu::pure]] inline MerkleHash compute_merkle_hash(TraceNode* node) noexcept {
    if (!node) return MerkleHash{};

    uint64_t merkle_hash_state;
    switch (node->kind) {
        case TraceNodeKind::REGION:
            merkle_hash_state = static_cast<RegionNode*>(node)->content_hash.raw();
            break;
        case TraceNodeKind::BRANCH: {
            auto* branch = static_cast<BranchNode*>(node);
            merkle_hash_state = detail::fmix64(branch->guard.hash());
            for (uint32_t i = 0; i < branch->num_arms; i++) {
                merkle_hash_state ^= detail::fmix64(branch->arms[i].target->merkle_hash.raw());
                merkle_hash_state *= 0x9E3779B97F4A7C15ULL;
            }
            break;
        }
        case TraceNodeKind::LOOP: {
            auto* loop = static_cast<LoopNode*>(node);
            // The salts keep a loop from hashing the same as a region with
            // the same content.  Mixing is fmix64 and XOR rather than wymix,
            // because wymix(x, 0) is 0 and would collapse the whole chain
            // whenever a feedback or termination component is zero.
            constexpr uint64_t kLoopSalt = 0x4C4F4F504E4F4445ULL;  // "LOOPNODE"
            constexpr uint64_t kFbSalt = 0x6665656462616B00ULL;  // "feedbak\0"
            merkle_hash_state = detail::fmix64(loop->body_content_hash.raw() ^ kLoopSalt);
            merkle_hash_state ^= detail::fmix64(feedback_signature(loop->feedback_span()) ^ kFbSalt);
            merkle_hash_state ^= loopterm_hash(*loop);
            break;
        }
        case TraceNodeKind::TERMINAL:
            return MerkleHash{};
        default:
            std::unreachable();
    }

    if (node->next) merkle_hash_state = detail::fmix64(merkle_hash_state ^ node->next->merkle_hash.raw());

    return MerkleHash{merkle_hash_state};
}

// Content hash for one kernel that fuses every arm of a branch.  Pure is
// sound: the guard and the arms' content hashes are fixed at construction.
[[nodiscard, gnu::pure]] inline ContentHash branched_content_hash(BranchNode* branch) noexcept {
    uint64_t branched_hash_state = detail::fmix64(branch->guard.hash());
    for (uint32_t i = 0; i < branch->num_arms; i++) {
        if (branch->arms[i].target->kind == TraceNodeKind::REGION) {
            branched_hash_state ^= detail::fmix64(static_cast<RegionNode*>(branch->arms[i].target)->content_hash.raw());
            branched_hash_state *= 0x9E3779B97F4A7C15ULL;
        }
    }
    return ContentHash{detail::fmix64(branched_hash_state)};
}

// Open-addressing map from (ContentHash, RowHash) to a compiled kernel.
// Capacity is a power of two so that `(slot + probe) & mask` wraps.
//
// The effect row is half the key, not a payload.  Two regions with identical
// ops but different rows must occupy different slots, because only some rows
// are shareable between machines.  A row mismatch is therefore a probe
// continuation and never a hit.  RowHash{0} is the bare-type baseline and is
// itself a valid key, not an absence.
//
// A slot is EMPTY (content 0), CLAIMED (content claimed by CAS, kernel still
// null) or PUBLISHED (both set).  Slots are insert-only.  row_hash is written
// once per slot.  A later publish under the same pair replaces only the
// kernel, so a concurrent reader sees one kernel or the other, both valid.
//
// The publish order is row_hash then kernel, both release.  kernel.store is
// therefore the last store of the sequence, and a reader that acquire-loads a
// non-null kernel is guaranteed to see the row that was published with it.
//
// Both lookup and insert spin on the kernel, never on row_hash, when they
// meet a CLAIMED slot.  row_hash defaults to 0 and 0 is also a legal row, so
// a spin on the row cannot tell "not published yet" from "published with row
// 0": an insert making that mistake would write into a slot whose row belongs
// to another inserter, and that inserter's own publish would then overwrite
// it, losing an insert that reported success.
//
// When the spin budget runs out the slot is treated as foreign and probing
// continues.  A stalled claim at one probe position must not mask a valid
// match further along the chain.  The worst case is a second slot for one
// pair, which wastes space and loses nothing.
class CRUCIBLE_OWNER KernelCache {
public:
    struct KernelCompileTag {};
    struct KernelCacheReaderTag {};

    struct KernelCacheSlotSnapshot {
        uint64_t content_hash = 0;
        uint64_t row_hash = 0;
        CompiledKernel* kernel = nullptr;
    };

    static_assert(sizeof(KernelCacheSlotSnapshot) == 24, "KernelCacheSlotSnapshot must stay the 24-byte wire triple: "
                                                         "8B content + 8B row + 8B kernel pointer.");
    static_assert(alignof(KernelCacheSlotSnapshot) == 8, "KernelCacheSlotSnapshot must stay naturally 8-byte aligned.");

    // A writer and reader surface over three plain atomics.  A single-writer
    // snapshot session cannot be embedded here: it isolates its sequence
    // counter and storage on separate cache lines and carries a reader pool,
    // both of which break the 24-byte slot.  Keeping the three atomics also
    // lets the hot lookup read the fields one at a time in probe order.
    //
    // A content hash of zero is the EMPTY marker, so every endpoint rejects
    // it.  Admitting zero would claim the wrong slot, publish a kernel that
    // any later claim silently overwrites, or hand a stale kernel back to a
    // fresh lookup.
    class KernelCacheSlot {
        friend class KernelCache;

    public:
        using snapshot_type = KernelCacheSlotSnapshot;
        using writer_tag = KernelCompileTag;
        using reader_tag = KernelCacheReaderTag;

        KernelCacheSlot() noexcept = default;

        class WriterHandle {
            KernelCacheSlot* slot_ = nullptr;
            [[no_unique_address]] fixy::perm::Permission<writer_tag> perm_;

            constexpr WriterHandle(KernelCacheSlot& slot, fixy::perm::Permission<writer_tag>&& perm) noexcept
                : slot_{&slot}, perm_{std::move(perm)} {}

            friend class KernelCacheSlot;

        public:
            using value_type = snapshot_type;
            using tag_type = writer_tag;

            WriterHandle(WriterHandle const&) =
                delete("KernelCacheSlot::WriterHandle owns the linear writer permission");
            WriterHandle&
            operator=(WriterHandle const&) = delete("KernelCacheSlot::WriterHandle owns the linear writer permission");
            constexpr WriterHandle(WriterHandle&&) noexcept = default;
            constexpr WriterHandle& operator=(WriterHandle&&) noexcept = default;

            void publish(snapshot_type const& snapshot) noexcept
                pre(::crucible::decide::is_non_zero(snapshot.content_hash)) pre(snapshot.kernel != nullptr) {
                // The content hash is already claimed by CAS before this
                // endpoint exists.  The two stores must stay in this order:
                // the kernel store is what makes the row visible to readers.
                contract_assert(slot_->content_hash_.load(std::memory_order_acquire) == snapshot.content_hash);
                slot_->row_hash_.store(snapshot.row_hash, std::memory_order_release);
                slot_->kernel_.store(snapshot.kernel, std::memory_order_release);
            }

            void publish_kernel_variant(CompiledKernel* kernel) noexcept pre(kernel != nullptr) {
                slot_->kernel_.store(kernel, std::memory_order_release);
            }
        };

        class ReaderHandle {
            KernelCacheSlot const* slot_ = nullptr;

            constexpr explicit ReaderHandle(KernelCacheSlot const& slot) noexcept : slot_{&slot} {}

            friend class KernelCacheSlot;

        public:
            using value_type = snapshot_type;
            using tag_type = reader_tag;

            [[nodiscard]] snapshot_type load() const noexcept {
                CompiledKernel* kernel = slot_->kernel_.load(std::memory_order_acquire);
                uint64_t row = 0;
                if (kernel != nullptr) {
                    row = slot_->row_hash_.load(std::memory_order_acquire);
                }
                return snapshot_type{
                    .content_hash = slot_->content_hash_.load(std::memory_order_acquire),
                    .row_hash = row,
                    .kernel = kernel,
                };
            }

            [[nodiscard]] CRUCIBLE_INLINE uint64_t content_hash() const noexcept {
                return slot_->content_hash_.load(std::memory_order_acquire);
            }

            [[nodiscard]] CRUCIBLE_INLINE uint64_t row_hash() const noexcept {
                return slot_->row_hash_.load(std::memory_order_acquire);
            }

            [[nodiscard]] CRUCIBLE_INLINE CompiledKernel* kernel() const noexcept {
                return slot_->kernel_.load(std::memory_order_acquire);
            }
        };

        [[nodiscard]] WriterHandle writer(fixy::perm::Permission<writer_tag>&& perm) noexcept {
            return WriterHandle{*this, std::move(perm)};
        }

        [[nodiscard]] ReaderHandle reader() const noexcept { return ReaderHandle{*this}; }

        [[nodiscard]] CRUCIBLE_INLINE uint64_t content_hash() const noexcept {
            return content_hash_.load(std::memory_order_acquire);
        }

        [[nodiscard]] CRUCIBLE_INLINE uint64_t row_hash() const noexcept {
            return row_hash_.load(std::memory_order_acquire);
        }

        [[nodiscard]] CRUCIBLE_INLINE CompiledKernel* kernel() const noexcept {
            return kernel_.load(std::memory_order_acquire);
        }

        [[nodiscard]] CRUCIBLE_INLINE bool try_claim_content_hash(uint64_t& expected, uint64_t desired) noexcept
            pre(::crucible::decide::is_non_zero(desired)) {
            return content_hash_.compare_exchange_strong(expected, desired, std::memory_order_acq_rel);
        }

    private:
        // Every access is acquire or release, never relaxed: a relaxed load
        // would let a reader match the content and then read an incoherent
        // row and kernel, which serves a kernel under the wrong row.
        //
        // An EMPTY slot also leaves row_hash at 0, which is harmless because
        // EMPTY is decided from content_hash == 0 before the row is read.
        std::atomic<uint64_t> content_hash_{0};
        std::atomic<uint64_t> row_hash_{0};
        std::atomic<CompiledKernel*> kernel_{nullptr};
    };

    static_assert(sizeof(KernelCacheSlot) == 24, "KernelCacheSlot must stay exactly 8B content + 8B row + "
                                                 "8B kernel pointer — the wire format matches this.");
    static_assert(alignof(KernelCacheSlot) == 8, "KernelCacheSlot must stay 8-byte aligned: atomic<uint64_t> and "
                                                 "atomic<ptr> need it, and over-alignment wastes cache.");
    static_assert(sizeof(KernelCacheSlot::WriterHandle) == sizeof(KernelCacheSlot*),
                  "KernelCacheSlot::WriterHandle must EBO-collapse its Permission.");

    enum class SlotState : uint8_t {
        Empty = 0,
        Claimed = 1,
        Published = 2,
    };

    explicit KernelCache(uint32_t capacity = 4096)
        // A power of two makes `(slot + probe) & mask` the wrap-around, and
        // the 2^31 ceiling keeps `slot_index + probe` inside uint32_t.
        pre(::crucible::decide::is_power_of_two_le<std::uint32_t>(capacity, std::uint32_t{1u << 31}))
        : capacity_(capacity) {
        // The clause above is armed in a release build as well as a debug
        // one: the release preset evaluates contracts under the `observe`
        // semantic, and the project's violation handler is noreturn and ends
        // in std::abort, so a violation stops the process either way.
        //
        // The repeat below is not therefore redundant. The semantic is a
        // per-target build option, and one target in this tree already sets
        // `ignore` — every contract in a header compiled into crucible_perf
        // evaluates to nothing. This check does not depend on that option,
        // and the whole probe sequence rests on the property it states:
        // `(slot + probe) & mask` only wraps back into the table when the
        // capacity is a power of two, so a capacity that is not one makes
        // every probe past the first read and write outside the allocation.
        // One construction per cache, so the repeat costs nothing.
        CRUCIBLE_FATAL_INVARIANT(capacity != 0 && (capacity & (capacity - 1)) == 0);
        table_ = allocate_table_(capacity_);
        if (!table_) [[unlikely]]
            std::abort();  // OOM is unrecoverable
        // No other thread holds a reference yet, so the relaxed load below
        // reads this thread's own store.
        size_.store(0, std::memory_order_relaxed);
        CRUCIBLE_POST(0, capacity_ == capacity);
        CRUCIBLE_POST(0, table_ != nullptr);
        CRUCIBLE_POST(0, size_.load(std::memory_order_relaxed) == 0);
    }

    ~KernelCache() { destroy_table_(table_, capacity_); }

    KernelCache(const KernelCache&) = delete("lock-free hash map with atomic state cannot be copied");
    KernelCache& operator=(const KernelCache&) = delete("lock-free hash map with atomic state cannot be copied");
    KernelCache(KernelCache&&) = delete("lock-free hash map with atomic state cannot be moved");
    KernelCache& operator=(KernelCache&&) = delete("lock-free hash map with atomic state cannot be moved");

    // Spin iterations a reader tolerates on a CLAIMED slot before treating it
    // as foreign.
    static constexpr uint32_t kClaimedSpinBudget = 64;

    // A lookup of the zero hash would walk the entire table: it matches no
    // slot and terminates on no empty one.  The row needs no such guard,
    // because RowHash{0} is a real key that can coexist with any other.
    CRUCIBLE_UNSAFE_BUFFER_USAGE [[nodiscard, gnu::hot]] CompiledKernel* lookup(ContentHash content_hash,
                                                                                RowHash row_hash) const noexcept
        CRUCIBLE_NO_THREAD_SAFETY pre(::crucible::decide::is_non_zero(content_hash)) {
        [[assume(content_hash.raw() != 0)]];
        const uint64_t lookup_hash = content_hash.raw();
        const uint64_t lookup_row = row_hash.raw();
        const uint32_t mask = capacity_ - 1;
        const uint32_t slot_index = static_cast<uint32_t>(lookup_hash) & mask;
        for (uint32_t probe = 0; probe < capacity_; probe++) {
            auto& entry = table_[(slot_index + probe) & mask];
            uint64_t key = entry.content_hash_.load(std::memory_order_acquire);
            if (key == 0) return nullptr;
            if (key != lookup_hash) continue;
            // The kernel must be loaded before the row: the release-acquire
            // pair on the kernel is what makes the published row visible.
            CompiledKernel* kernel_ptr = entry.kernel_.load(std::memory_order_acquire);
            if (kernel_ptr == nullptr) [[unlikely]] {
                kernel_ptr = await_claimed_(entry);
                if (kernel_ptr == nullptr) [[unlikely]] {
                    // One content hash can occupy several slots, one per row,
                    // so a stalled claim here must not hide a matching slot
                    // further along the chain.
                    continue;
                }
            }
            uint64_t entry_row = entry.row_hash_.load(std::memory_order_acquire);
            if (entry_row == lookup_row) [[likely]]
                return kernel_ptr;
        }
        return nullptr;
    }

    enum class InsertError : uint8_t {
        TableFull,
        NotYetImplemented,
    };

    // Zero collides with the EMPTY marker, and UINT64_MAX is reserved as the
    // end-of-region marker, so neither can be a key.  The row again needs no
    // guard: RowHash{0} is a real key.
    CRUCIBLE_UNSAFE_BUFFER_USAGE [[nodiscard]] std::expected<void, InsertError>
    insert(ContentHash content_hash, RowHash row_hash, CompiledKernel* kernel)
        CRUCIBLE_NO_THREAD_SAFETY pre(::crucible::decide::is_non_zero(content_hash))
            pre(::crucible::decide::not_sentinel_hash(content_hash)) pre(kernel != nullptr) {
        const uint64_t lookup_hash = content_hash.raw();
        const uint64_t lookup_row = row_hash.raw();
        const uint32_t mask = capacity_ - 1;
        const uint32_t slot_index = static_cast<uint32_t>(lookup_hash) & mask;
        for (uint32_t probe = 0; probe < capacity_; probe++) {
            auto& entry = table_[(slot_index + probe) & mask];
            uint64_t expected = 0;
            // A successful CAS claims the slot for this thread.
            if (entry.try_claim_content_hash(expected, lookup_hash)) {
                auto writer = entry.writer(fixy::perm::mint_permission_root<KernelCompileTag>());
                writer.publish(KernelCacheSlotSnapshot{
                    .content_hash = lookup_hash,
                    .row_hash = lookup_row,
                    .kernel = kernel,
                });
                // Relaxed: nothing branches on the exact count.  The content
                // hash CAS carries the real synchronization.
                size_.fetch_add(1, std::memory_order_relaxed);
                return {};
            }
            if (expected == lookup_hash) {
                // The content matches, so the row decides.  Spin on the
                // kernel, never on the row: a CLAIMED slot reads row 0, which
                // is indistinguishable from a published row of 0.
                CompiledKernel* existing_kernel = entry.kernel();
                for (uint32_t spin = 0; spin < kClaimedSpinBudget && existing_kernel == nullptr; ++spin) {
                    CRUCIBLE_SPIN_PAUSE;
                    existing_kernel = entry.kernel();
                }
                if (existing_kernel != nullptr) {
                    // A published kernel makes the row visible too.
                    uint64_t existing_row = entry.row_hash();
                    if (existing_row == lookup_row) {
                        // Variant update: the row stays pinned and only the
                        // kernel changes, so a concurrent reader observes one
                        // kernel or the other and both are valid.
                        auto writer = entry.writer(fixy::perm::mint_permission_root<KernelCompileTag>());
                        writer.publish_kernel_variant(kernel);
                        return {};
                    }
                }
            }
        }
        return std::unexpected(InsertError::TableFull);
    }

    // The cache is three tiers: L1 is the vendor-neutral working set held in
    // memory, L2 the per-vendor-family store, L3 the per-chip archive of
    // compiled bytes.  Only L1 has a backing store.  The other two exist at
    // the type level so call sites already speak in tiers.  Their lookups
    // find nothing and their publishes report NotYetImplemented, which is
    // what a caller must branch on — a vacuous success would let a path that
    // depends on persistence miss every later lookup in silence.
    //
    // Pinning the tier in the return type is what stops a value read from the
    // cold archive being handed to a path that requires a resident one.
    CRUCIBLE_UNSAFE_BUFFER_USAGE [[nodiscard, gnu::hot]] fixy::wrap::residency_heat::Hot<CompiledKernel*>
    lookup_l1(ContentHash content_hash, RowHash row_hash) const noexcept
        CRUCIBLE_NO_THREAD_SAFETY pre(::crucible::decide::is_non_zero(content_hash)) {
        return fixy::wrap::residency_heat::Hot<CompiledKernel*>{lookup(content_hash, row_hash)};
    }

    // The preconditions on the two tiers below match L1's even though the
    // bodies ignore their arguments: a backing store added later inherits the
    // contract, whereas relaxing it later would have to be renegotiated at
    // every call site.
    [[nodiscard]] fixy::wrap::residency_heat::Warm<CompiledKernel*> lookup_l2(ContentHash content_hash,
                                                                              RowHash /*row_hash*/) const noexcept
        CRUCIBLE_NO_THREAD_SAFETY pre(::crucible::decide::is_non_zero(content_hash)) {
        (void)content_hash;
        return fixy::wrap::residency_heat::Warm<CompiledKernel*>{nullptr};
    }

    [[nodiscard]] fixy::wrap::residency_heat::Cold<CompiledKernel*> lookup_l3(ContentHash content_hash,
                                                                              RowHash /*row_hash*/) const noexcept
        CRUCIBLE_NO_THREAD_SAFETY pre(::crucible::decide::is_non_zero(content_hash)) {
        (void)content_hash;
        return fixy::wrap::residency_heat::Cold<CompiledKernel*>{nullptr};
    }

    // A caller derives row_hash by projecting a typed effect row, so that the
    // cache key stays expressible in the row vocabulary.  A literal RowHash
    // is accepted for the row-blind baseline of RowHash{0}.
    //
    // Both hash guards are needed here and on the two tiers below: UINT64_MAX
    // is non-zero, and zero is not the reserved sentinel.
    CRUCIBLE_UNSAFE_BUFFER_USAGE [[nodiscard]]
    fixy::wrap::residency_heat::Hot<std::expected<void, InsertError>>
    publish_l1(ContentHash content_hash, RowHash row_hash, CompiledKernel* kernel)
        CRUCIBLE_NO_THREAD_SAFETY pre(::crucible::decide::is_non_zero(content_hash))
            pre(::crucible::decide::not_sentinel_hash(content_hash)) pre(kernel != nullptr) {
        return fixy::wrap::residency_heat::Hot<std::expected<void, InsertError>>{
            insert(content_hash, row_hash, kernel)};
    }

    [[nodiscard]]
    fixy::wrap::residency_heat::Warm<std::expected<void, InsertError>>
    publish_l2(ContentHash content_hash, RowHash /*row_hash*/, CompiledKernel* kernel) noexcept
        pre(::crucible::decide::is_non_zero(content_hash)) pre(::crucible::decide::not_sentinel_hash(content_hash))
            pre(kernel != nullptr) {
        (void)content_hash;
        (void)kernel;
        return fixy::wrap::residency_heat::Warm<std::expected<void, InsertError>>{
            std::unexpected(InsertError::NotYetImplemented)};
    }

    [[nodiscard]]
    fixy::wrap::residency_heat::Cold<std::expected<void, InsertError>>
    publish_l3(ContentHash content_hash, RowHash /*row_hash*/, CompiledKernel* kernel) noexcept
        pre(::crucible::decide::is_non_zero(content_hash)) pre(::crucible::decide::not_sentinel_hash(content_hash))
            pre(kernel != nullptr) {
        (void)content_hash;
        (void)kernel;
        return fixy::wrap::residency_heat::Cold<std::expected<void, InsertError>>{
            std::unexpected(InsertError::NotYetImplemented)};
    }

    // Relaxed: an informational counter that nothing orders against.
    [[nodiscard]] uint32_t size() const CRUCIBLE_NO_THREAD_SAFETY { return size_.load(std::memory_order_relaxed); }
    [[nodiscard]] uint32_t capacity() const { return capacity_; }

    // The capacity guard is what keeps `capacity_ - 1u` below from wrapping
    // to UINT32_MAX on an unconstructed cache.
    [[nodiscard]] SlotState diag_slot_state(uint32_t slot_index) const noexcept CRUCIBLE_NO_THREAD_SAFETY {
        CRUCIBLE_PRE(capacity_ > 0u);
        CRUCIBLE_PRE(::crucible::decide::in_range<uint32_t>(slot_index, 0u, capacity_ - 1u));
        const KernelCacheSlot& entry = table_[slot_index];
        return classify_(entry.content_hash_.load(std::memory_order_acquire),
                         entry.kernel_.load(std::memory_order_acquire));
    }

private:
    [[nodiscard, gnu::const]] static constexpr SlotState classify_(uint64_t hash_bits,
                                                                   const CompiledKernel* k) noexcept {
        if (hash_bits == 0) return SlotState::Empty;
        if (k == nullptr) return SlotState::Claimed;
        return SlotState::Published;
    }

    static constexpr std::size_t kTableAlignment = 64;
    static_assert(kTableAlignment % alignof(KernelCacheSlot) == 0);

    [[nodiscard]] static KernelCacheSlot* allocate_table_(uint32_t capacity) noexcept {
        const auto bytes = static_cast<std::size_t>(capacity) * sizeof(KernelCacheSlot);
        void* raw = ::operator new(bytes, std::align_val_t{kTableAlignment}, std::nothrow);
        if (raw == nullptr) return nullptr;
        auto* slots = static_cast<KernelCacheSlot*>(raw);
        for (uint32_t i = 0; i < capacity; ++i) {
            ::new(static_cast<void*>(&slots[i])) KernelCacheSlot();
        }
        return slots;
    }

    static void destroy_table_(KernelCacheSlot* table, uint32_t capacity) noexcept {
        if (table == nullptr) return;
        for (uint32_t i = 0; i < capacity; ++i) {
            table[i].~KernelCacheSlot();
        }
        ::operator delete(static_cast<void*>(table), std::align_val_t{kTableAlignment});
    }

    // Outlined so that the hot lookup body stays compact.
    CRUCIBLE_UNSAFE_BUFFER_USAGE [[gnu::cold, gnu::noinline]]
    static CompiledKernel* await_claimed_(const KernelCacheSlot& entry) noexcept {
        for (uint32_t spin = 0; spin < kClaimedSpinBudget; ++spin) {
            CRUCIBLE_SPIN_PAUSE;
            CompiledKernel* kernel_ptr = entry.kernel_.load(std::memory_order_acquire);
            if (kernel_ptr != nullptr) return kernel_ptr;
        }
        return nullptr;
    }

    KernelCacheSlot* table_;
    uint32_t capacity_;
    std::atomic<uint32_t> size_;
};

// The ops array is stored, not copied: the caller keeps it alive for as long
// as the node.
[[nodiscard]] inline RegionNode* make_region(effects::Alloc a, Arena& arena CRUCIBLE_LIFETIMEBOUND, TraceEntry* ops,
                                             uint32_t num_ops) noexcept
    pre(::crucible::decide::valid_span(num_ops, ops)) {
    auto* node = new(arena.alloc_obj<RegionNode>(a)) RegionNode{};
    node->kind = TraceNodeKind::REGION;
    node->ops = ops;
    node->num_ops = num_ops;
    // The node was just default-constructed, so a non-zero hash here means a
    // second construction over a live node.
    contract_assert(node->content_hash.raw() == 0);
    node->content_hash = compute_content_hash(std::span{ops, num_ops});
    node->first_op_schema = (num_ops > 0) ? ops[0].schema_hash : SchemaHash{};
    CRUCIBLE_POST(node, node != nullptr);
    CRUCIBLE_POST(node, node->kind == TraceNodeKind::REGION);
    CRUCIBLE_POST(node, node->ops == ops);
    CRUCIBLE_POST(node, node->num_ops == num_ops);
    CRUCIBLE_POST(node, ::crucible::decide::implies(num_ops > 0u, node->content_hash.raw() != 0));
    return node;
}

// For a caller that has already folded the hash while streaming the ops.
[[nodiscard]] inline RegionNode* make_region(effects::Alloc a, Arena& arena CRUCIBLE_LIFETIMEBOUND, TraceEntry* ops,
                                             uint32_t num_ops, ContentHash precomputed_hash) noexcept
    pre(::crucible::decide::valid_span(num_ops, ops))
        pre(::crucible::decide::is_non_zero(precomputed_hash) || num_ops == 0) {
    auto* node = new(arena.alloc_obj<RegionNode>(a)) RegionNode{};
    node->kind = TraceNodeKind::REGION;
    node->ops = ops;
    node->num_ops = num_ops;
    contract_assert(node->content_hash.raw() == 0);
    node->content_hash = precomputed_hash;
    node->first_op_schema = (num_ops > 0) ? ops[0].schema_hash : SchemaHash{};
    CRUCIBLE_POST(node, node != nullptr);
    CRUCIBLE_POST(node, node->kind == TraceNodeKind::REGION);
    CRUCIBLE_POST(node, node->ops == ops);
    CRUCIBLE_POST(node, node->num_ops == num_ops);
    CRUCIBLE_POST(node, node->content_hash == precomputed_hash);
    return node;
}

// The recipe is borrowed, and the node does not keep it: it participates in
// the content hash and is then dropped, so a caller that needs to recover the
// recipe later must track it separately.  A null recipe is rejected rather
// than accepted, because the overload above already covers that case.
[[nodiscard]] inline RegionNode* make_region(effects::Alloc a, Arena& arena CRUCIBLE_LIFETIMEBOUND, TraceEntry* ops,
                                             uint32_t num_ops, const NumericalRecipe* recipe) noexcept
    pre(::crucible::decide::valid_span(num_ops, ops)) pre(recipe != nullptr)
        pre(::crucible::decide::is_non_zero(recipe->hash)) pre(!recipe->hash.is_sentinel()) {
    auto* node = new(arena.alloc_obj<RegionNode>(a)) RegionNode{};
    node->kind = TraceNodeKind::REGION;
    node->ops = ops;
    node->num_ops = num_ops;
    contract_assert(node->content_hash.raw() == 0);
    node->content_hash = compute_content_hash(std::span{ops, num_ops}, recipe);
    node->first_op_schema = (num_ops > 0) ? ops[0].schema_hash : SchemaHash{};
    CRUCIBLE_POST(node, node != nullptr);
    CRUCIBLE_POST(node, node->kind == TraceNodeKind::REGION);
    CRUCIBLE_POST(node, node->ops == ops);
    CRUCIBLE_POST(node, node->num_ops == num_ops);
    CRUCIBLE_POST(node, ::crucible::decide::implies(num_ops > 0u, node->content_hash.raw() != 0));
    return node;
}

[[nodiscard]] inline TraceNode* make_terminal(effects::Alloc a, Arena& arena) noexcept {
    auto* node = new(arena.alloc_obj<TraceNode>(a)) TraceNode{};
    node->kind = TraceNodeKind::TERMINAL;
    CRUCIBLE_POST(node, node != nullptr);
    CRUCIBLE_POST(node, node->kind == TraceNodeKind::TERMINAL);
    return node;
}

// The body must be a complete sub-DAG ending in TERMINAL, and
// body_content_hash must already be folded from that body's region chain.
[[nodiscard]] inline LoopNode* make_loop(effects::Alloc a, Arena& arena CRUCIBLE_LIFETIMEBOUND, TraceNode* body,
                                         ContentHash body_content_hash, FeedbackEdge* feedback, uint16_t num_feedback,
                                         LoopTermKind term_kind, uint32_t repeat_count, float epsilon = 0.0f) noexcept
    pre(body != nullptr) pre(::crucible::decide::valid_span(num_feedback, feedback))
    // A convergence distance cannot be negative, and the sign bit of epsilon
    // reaches the termination hash, so two loops that behave identically
    // would otherwise hash differently.  A repeat count of zero stays legal:
    // it is the "skip the body, run the continuation" case.  An epsilon of
    // zero is also accepted here, and the caller sets a real threshold before
    // the loop runs.
    pre(!(epsilon < 0.0f)) {
    [[assume(body != nullptr)]];
    auto* node = new(arena.alloc_obj<LoopNode>(a)) LoopNode{};
    node->kind = TraceNodeKind::LOOP;
    node->body = body;
    node->body_content_hash = body_content_hash;
    node->feedback_edges = feedback;
    node->num_feedback = num_feedback;
    node->term_kind = term_kind;
    node->repeat_count = repeat_count;
    node->epsilon = epsilon;
    CRUCIBLE_POST(node, node != nullptr);
    CRUCIBLE_POST(node, node->kind == TraceNodeKind::LOOP);
    CRUCIBLE_POST(node, node->body == body);
    CRUCIBLE_POST(node, node->feedback_edges == feedback);
    CRUCIBLE_POST(node, node->num_feedback == num_feedback);
    CRUCIBLE_POST(node, node->term_kind == term_kind);
    CRUCIBLE_POST(node, node->repeat_count == repeat_count);
    return node;
}

// Children are recomputed first, because a node's hash folds in the hashes of
// its arms, its body and its continuation.
inline void recompute_merkle(TraceNode* node) {
    if (!node) return;
    if (node->kind == TraceNodeKind::BRANCH) {
        auto* branch = static_cast<BranchNode*>(node);
        for (uint32_t i = 0; i < branch->num_arms; i++)
            recompute_merkle(branch->arms[i].target);
    } else if (node->kind == TraceNodeKind::LOOP) {
        recompute_merkle(static_cast<LoopNode*>(node)->body);
    }
    if (node->next) recompute_merkle(node->next);

    node->merkle_hash = compute_merkle_hash(node);
}

[[nodiscard]] inline uint32_t collect_regions(TraceNode* node, std::span<RegionNode*> out, uint32_t count = 0) {
    while (node && count < out.size()) {
        switch (node->kind) {
            case TraceNodeKind::REGION:
                out[count++] = static_cast<RegionNode*>(node);
                break;
            case TraceNodeKind::BRANCH: {
                auto* branch = static_cast<BranchNode*>(node);
                for (uint32_t i = 0; i < branch->num_arms; i++)
                    count = collect_regions(branch->arms[i].target, out, count);
                break;
            }
            case TraceNodeKind::LOOP:
                count = collect_regions(static_cast<LoopNode*>(node)->body, out, count);
                break;
            case TraceNodeKind::TERMINAL:
                break;  // not a stop condition here: the walk follows next
            default:
                std::unreachable();
        }
        node = node->next;
    }
    return count;
}

// Returns the first node of the shared suffix, or null when the new ops and
// the existing continuation share no tail.
[[nodiscard]] inline TraceNode* find_merge_point(std::span<TraceEntry> new_ops, TraceNode* existing_continuation) {
    constexpr uint32_t MAX_REGIONS = 1024;
    RegionNode* existing_regions[MAX_REGIONS];
    uint32_t num_existing = 0;

    TraceNode* walk = existing_continuation;
    while (walk && walk->kind == TraceNodeKind::REGION && num_existing < MAX_REGIONS) {
        existing_regions[num_existing++] = static_cast<RegionNode*>(walk);
        walk = walk->next;
    }

    if (num_existing == 0) return nullptr;

    TraceNode* merge = nullptr;
    uint32_t new_pos = static_cast<uint32_t>(new_ops.size());
    uint32_t ex_idx = num_existing;

    while (ex_idx > 0 && new_pos > 0) {
        ex_idx--;
        RegionNode* region = existing_regions[ex_idx];
        if (new_pos < region->num_ops) break;

        ContentHash new_hash = compute_content_hash(new_ops.subspan(new_pos - region->num_ops, region->num_ops));

        if (new_hash == region->content_hash) {
            merge = region;
            new_pos -= region->num_ops;
        } else {
            break;
        }
    }

    return merge;
}

// Splits a straight-line trace at a divergence point into a two-arm branch
// that rejoins where the continuations become identical again.
[[nodiscard]] inline BranchNode*
add_branch(effects::Alloc a, Arena& arena, KernelCache& kernel_cache, TraceNode* divergence_point, TraceEntry* new_ops,
           uint32_t new_n, int64_t old_guard_value, int64_t new_guard_value, Guard guard, TraceNode* existing_suffix)
    pre(divergence_point != nullptr) pre(old_guard_value != new_guard_value) {
    auto* new_region = make_region(a, arena, new_ops, new_n);

    TraceNode* merge = find_merge_point(std::span{new_ops, new_n}, existing_suffix);

    new_region->next = merge;

    // The dispatch path does not yet carry an effect row down to here, so the
    // lookup uses the bare-type row.  Every untyped caller therefore agrees
    // on one slot, and row-tagged callers land in disjoint slots rather than
    // sharing this one.  A miss leaves compiled at its default null, which is
    // the right state for a region with no kernel yet.
    if (auto* cached_kernel = kernel_cache.lookup(new_region->content_hash, RowHash{0})) {
        new_region->compiled.publish(cached_kernel);
    }

    auto* branch = arena.alloc_obj<BranchNode>(a);
    ::new(branch) BranchNode{};
    branch->kind = TraceNodeKind::BRANCH;
    branch->guard = guard;
    branch->num_arms = 2;
    branch->arms = arena.alloc_array<BranchNode::Arm>(a, 2);
    // Arms go in sorted by value: replay binary-searches them.
    if (old_guard_value <= new_guard_value) {
        branch->arms[0] = {.value = old_guard_value, .target = divergence_point};
        branch->arms[1] = {.value = new_guard_value, .target = new_region};
    } else {
        branch->arms[0] = {.value = new_guard_value, .target = new_region};
        branch->arms[1] = {.value = old_guard_value, .target = divergence_point};
    }
    branch->next = merge;

    recompute_merkle(branch);

    CRUCIBLE_POST(branch, branch != nullptr);
    CRUCIBLE_POST(branch, branch->kind == TraceNodeKind::BRANCH);
    CRUCIBLE_POST(branch, branch->num_arms == 2u);
    CRUCIBLE_POST(branch, branch->arms != nullptr);
    return branch;
}

// GuardEval is int64_t(const Guard&) and RegionExec is void(RegionNode*).
template <typename GuardEval, typename RegionExec>
[[nodiscard, gnu::flatten]] inline bool replay(TraceNode* node, GuardEval&& eval_guard, RegionExec&& exec_region) {
    while (node) {
        switch (node->kind) {
            case TraceNodeKind::REGION: {
                auto* region = static_cast<RegionNode*>(node);
                exec_region(region);
                node = node->next;
                break;
            }

            case TraceNodeKind::BRANCH: {
                auto* branch = static_cast<BranchNode*>(node);
                int64_t val = eval_guard(branch->guard);

                contract_assert(branch->are_arms_sorted_by_value());

                TraceNode* arm = nullptr;
                {
                    uint32_t lo = 0, hi = branch->num_arms;
                    while (lo < hi) {
                        uint32_t mid = lo + (hi - lo) / 2;
                        // mid is in [lo, hi) and hi is at most num_arms.
                        [[assume(mid < branch->num_arms)]];
                        int64_t mid_val = branch->arms[mid].value;
                        if (mid_val < val)
                            lo = mid + 1;
                        else if (mid_val > val)
                            hi = mid;
                        else {
                            arm = branch->arms[mid].target;
                            break;
                        }
                    }
                }

                if (!arm) return false;  // unseen guard value: the caller falls back to recording

                if (!replay(arm, eval_guard, exec_region)) return false;

                node = branch->next;
                break;
            }

            case TraceNodeKind::LOOP: {
                auto* loop = static_cast<LoopNode*>(node);
                // Armed in release. A null body does not fault: the
                // recursive call walks `while (node)` zero times and returns
                // true, so the loop reports a successful replay of a body
                // that never ran. A wrong answer is worse than a trap, and
                // this is a cold path, so the check stays in every build.
                CRUCIBLE_FATAL_INVARIANT(loop->body != nullptr);
                for (uint32_t i = 0; i < loop->repeat_count; i++) {
                    if (!replay(loop->body, eval_guard, exec_region)) return false;
                }
                node = node->next;
                break;
            }

            case TraceNodeKind::TERMINAL:
                return true;

            default:
                std::unreachable();
        }
    }
    return true;
}

struct DagDiff {
    TraceNode* node_a = nullptr;
    TraceNode* node_b = nullptr;
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
[[nodiscard]] inline DagDiff dag_diff(TraceNode* a, TraceNode* b, uint32_t depth = 0) {
    while (a && b) {
        // Equal hashes mean the whole subtree below is equal.
        if (a->merkle_hash == b->merkle_hash) return {nullptr, nullptr, depth, DagDiff::Kind::IDENTICAL};

        if (a->kind != b->kind) return {a, b, depth, DagDiff::Kind::KIND_MISMATCH};

        if (a->kind == TraceNodeKind::TERMINAL) return {nullptr, nullptr, depth, DagDiff::Kind::IDENTICAL};

        if (a->kind == TraceNodeKind::REGION) {
            auto* ra = static_cast<RegionNode*>(a);
            auto* rb = static_cast<RegionNode*>(b);
            if (ra->content_hash != rb->content_hash) return {a, b, depth, DagDiff::Kind::CONTENT_MISMATCH};
            a = a->next;
            b = b->next;
            ++depth;
            continue;
        }

        if (a->kind == TraceNodeKind::BRANCH) {
            auto* ba = static_cast<BranchNode*>(a);
            auto* bb = static_cast<BranchNode*>(b);
            if (ba->guard.hash() != bb->guard.hash() || ba->num_arms != bb->num_arms)
                return {a, b, depth, DagDiff::Kind::BRANCH_MISMATCH};
            for (uint32_t i = 0; i < ba->num_arms; i++) {
                if (ba->arms[i].value != bb->arms[i].value) return {a, b, depth, DagDiff::Kind::BRANCH_MISMATCH};
                DagDiff arm_diff = dag_diff(ba->arms[i].target, bb->arms[i].target, depth + 1);
                if (arm_diff.kind != DagDiff::Kind::IDENTICAL) return arm_diff;
            }
            a = a->next;
            b = b->next;
            ++depth;
            continue;
        }

        if (a->kind == TraceNodeKind::LOOP) {
            auto* la = static_cast<LoopNode*>(a);
            auto* lb = static_cast<LoopNode*>(b);
            if (la->body_content_hash != lb->body_content_hash || la->num_feedback != lb->num_feedback
                || la->term_kind != lb->term_kind || la->repeat_count != lb->repeat_count
                || std::bit_cast<uint32_t>(la->epsilon) != std::bit_cast<uint32_t>(lb->epsilon))
                return {a, b, depth, DagDiff::Kind::LOOP_MISMATCH};
            if (la->num_feedback > 0
                && std::memcmp(la->feedback_edges, lb->feedback_edges, la->num_feedback * sizeof(FeedbackEdge)) != 0)
                return {a, b, depth, DagDiff::Kind::LOOP_MISMATCH};
            // The bodies agree on content, so compare their structure.
            DagDiff body_diff = dag_diff(la->body, lb->body, depth + 1);
            if (body_diff.kind != DagDiff::Kind::IDENTICAL) return body_diff;
            a = a->next;
            b = b->next;
            ++depth;
            continue;
        }

        return {a, b, depth, DagDiff::Kind::KIND_MISMATCH};
    }

    if (a == b)  // both null: the two chains ended together
        return {nullptr, nullptr, depth, DagDiff::Kind::IDENTICAL};
    return {a, b, depth, DagDiff::Kind::STRUCTURE_MISMATCH};
}

[[nodiscard]] inline uint32_t dag_node_count(TraceNode* node) {
    uint32_t count = 0;
    while (node) {
        ++count;
        switch (node->kind) {
            case TraceNodeKind::BRANCH: {
                auto* b = static_cast<BranchNode*>(node);
                for (uint32_t i = 0; i < b->num_arms; i++)
                    count += dag_node_count(b->arms[i].target);
                break;
            }
            case TraceNodeKind::LOOP:
                count += dag_node_count(static_cast<LoopNode*>(node)->body);
                break;
            case TraceNodeKind::REGION:
            case TraceNodeKind::TERMINAL:
                break;
            default:
                std::unreachable();
        }
        node = node->next;
    }
    return count;
}

}  // namespace crucible
