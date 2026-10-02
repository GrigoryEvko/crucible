#pragma once

#include <crucible/Arena.h>
#include <crucible/CKernelId.h>
#include <crucible/DimHash.h>
#include <crucible/Expr.h>
#include <crucible/NumericalRecipe.h>
#include <crucible/Reflect.h>
#include <crucible/StorageNbytes.h>
#include <crucible/TensorMeta.h>
#include <crucible/Types.h>
#include <fixy/Bands.h>
#include <fixy/Borrowed.h>
#include <fixy/Mutation.h>
#include <fixy/Refined.h>
#include <fixy/Saturated.h>
#include <fixy/handle/PublishOnce.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Decide.h>
#include <foundation/contracts/Post.h>
#include <foundation/contracts/Pre.h>
#include <foundation/effects/Effect.h>

#include <array>
#include <bit>
#include <cstdlib>
#include <cstdint>
#include <cstring>
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
    std::array<::foundation::decide::Interval<std::uint64_t>, MaxLive> live{};
    std::size_t n = 0;
    const std::uint32_t t = op.raw();
    for (TensorSlot const& s : slots) {
        if (s.is_external) continue;
        const std::uint32_t birth = s.birth_op.raw();
        const std::uint32_t death = s.death_op.raw();
        if (birth <= t && t <= death) {
            if (n >= MaxLive) return false;
            if (!::foundation::decide::no_overflow_sum(s.offset_bytes, s.nbytes)) return false;
            live[n] = {.lo = s.offset_bytes, .hi = s.offset_bytes + s.nbytes};
            ++n;
        }
    }
    return ::foundation::decide::intervals_pairwise_disjoint(
        std::span<const ::foundation::decide::Interval<std::uint64_t>>(live.data(), n));
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

    [[nodiscard]] ::fixy::Borrowed<const TensorMeta, TraceEntry> input_span() const CRUCIBLE_LIFETIMEBOUND {
        return input_metas ? ::fixy::Borrowed<const TensorMeta, TraceEntry>{input_metas, num_inputs}
                           : ::fixy::Borrowed<const TensorMeta, TraceEntry>{};
    }
    [[nodiscard]] ::fixy::Borrowed<TensorMeta, TraceEntry> input_span() CRUCIBLE_LIFETIMEBOUND {
        return input_metas ? ::fixy::Borrowed<TensorMeta, TraceEntry>{input_metas, num_inputs}
                           : ::fixy::Borrowed<TensorMeta, TraceEntry>{};
    }
    [[nodiscard]] ::fixy::Borrowed<const TensorMeta, TraceEntry> output_span() const CRUCIBLE_LIFETIMEBOUND {
        return output_metas ? ::fixy::Borrowed<const TensorMeta, TraceEntry>{output_metas, num_outputs}
                            : ::fixy::Borrowed<const TensorMeta, TraceEntry>{};
    }
    [[nodiscard]] ::fixy::Borrowed<TensorMeta, TraceEntry> output_span() CRUCIBLE_LIFETIMEBOUND {
        return output_metas ? ::fixy::Borrowed<TensorMeta, TraceEntry>{output_metas, num_outputs}
                            : ::fixy::Borrowed<TensorMeta, TraceEntry>{};
    }
    [[nodiscard]] ::fixy::Borrowed<const int64_t, TraceEntry> scalar_span() const CRUCIBLE_LIFETIMEBOUND {
        return scalar_args ? ::fixy::Borrowed<const int64_t, TraceEntry>{scalar_args, num_scalar_args}
                           : ::fixy::Borrowed<const int64_t, TraceEntry>{};
    }
    [[nodiscard]] ::fixy::Borrowed<const OpIndex, TraceEntry> trace_index_span() const CRUCIBLE_LIFETIMEBOUND {
        return input_trace_indices ? ::fixy::Borrowed<const OpIndex, TraceEntry>{input_trace_indices, num_inputs}
                                   : ::fixy::Borrowed<const OpIndex, TraceEntry>{};
    }
    [[nodiscard]] ::fixy::Borrowed<const SlotId, TraceEntry> input_slot_span() const CRUCIBLE_LIFETIMEBOUND {
        return input_slot_ids ? ::fixy::Borrowed<const SlotId, TraceEntry>{input_slot_ids, num_inputs}
                              : ::fixy::Borrowed<const SlotId, TraceEntry>{};
    }
    [[nodiscard]] ::fixy::Borrowed<const SlotId, TraceEntry> output_slot_span() const CRUCIBLE_LIFETIMEBOUND {
        return output_slot_ids ? ::fixy::Borrowed<const SlotId, TraceEntry>{output_slot_ids, num_outputs}
                               : ::fixy::Borrowed<const SlotId, TraceEntry>{};
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
// silently wrong instead of failing.  Minting this refinement is the
// validation point, and make_trace_node_kind consumes the proof.  The one
// door is ::fixy::mint_refined<kValidTraceNodeKindBound>(byte).
inline constexpr auto kValidTraceNodeKindBound = ::fixy::bounded_above<static_cast<uint8_t>(TraceNodeKind::TERMINAL)>;

using ValidTraceNodeKindRaw = ::fixy::Refined<kValidTraceNodeKindBound, uint8_t>;

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
    [[nodiscard]] ::fixy::Refined<::fixy::non_zero, MerkleHash> computed_merkle_hash() const noexcept {
        CRUCIBLE_PRE(::foundation::decide::is_non_zero(merkle_hash));
        return ::fixy::mint_refined<::fixy::non_zero>(merkle_hash);
    }
};

CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(TraceNode);

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
    ContentHash content_hash;
    // One writer publishes the compiled kernel once and every reader
    // acquire-loads it.  sizeof(PublishOnce<T*>) equals sizeof(atomic<T*>),
    // so the 80-byte layout below holds.
    ::fixy::handle::PublishOnce<CompiledKernel> compiled;

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
    using VariantCounter = ::fixy::Monotonic<uint32_t>;
    VariantCounter variant_id = ::fixy::mint_monotonic<uint32_t>(0u);

    MemoryPlan* plan = nullptr;  // null until liveness analysis runs

    void set_measured_ms(float ms) noexcept {
        CRUCIBLE_PRE(ms >= 0.0f);  // >= also rejects NaN
        CRUCIBLE_PRE(!__builtin_isinf(ms));
        measured_ms = ms;
    }

    [[nodiscard, gnu::pure]] bool has_measurement() const noexcept { return measured_ms > 0.0f; }

    [[nodiscard, gnu::pure]] bool has_variant() const noexcept { return variant_id.get() != 0u; }

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

// Carries one of the loop body's outputs back to one of its inputs for the
// next iteration.
struct FeedbackEdge {
    uint16_t output_idx = 0;
    uint16_t input_idx = 0;
};

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
    [[nodiscard]] ValidContentHash computed_body_content_hash() const noexcept {
        CRUCIBLE_PRE(::foundation::decide::is_non_zero(body_content_hash));
        return ::fixy::mint_refined<::fixy::non_zero>(body_content_hash);
    }
};

CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(LoopNode);

// An empty edge set hashes to 0 and any non-empty set hashes non-zero.  The
// non-zero seed is what makes the first claim hold: fmix64 is a permutation,
// so only one accumulator state maps to 0, and the seed is not it.  The edge
// count folds in as well, so {A} and {A, A} differ.
CRUCIBLE_PURE inline uint64_t feedback_signature(std::span<const FeedbackEdge> edges) noexcept {
    if (edges.empty()) return 0;
    constexpr uint64_t kSeed = 0x6665656462616B73ULL;  // "feedbaks"
    uint64_t signature_state = kSeed;
    for (const auto& edge : edges) {
        signature_state = detail::combine_ids(signature_state, reflect_hash(edge));
    }
    signature_state = detail::combine_ids(signature_state, edges.size());
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
            body_hash_state = detail::combine_ids(body_hash_state, static_cast<RegionNode*>(walk)->content_hash.raw());
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
    // The tag a producer passes to say that it selects no numerics at all,
    // as opposed to having forgotten to pass the recipe it does have.
    //
    // The two are the same 64 bits and always were, because the no-recipe
    // path folds nothing.  What the tag buys is that they are no longer the
    // same SOURCE CODE.  Under the old shape the recipe arrived through an
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
        [[assume(recipe.hash.raw() != 0)]];
        [[assume(recipe.hash.raw() != UINT64_MAX)]];
        state_ = detail::combine_ids(state_, recipe.hash.raw());
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
    // Every step is the same chain: `state = fmix64(state ^ input)`.  Three
    // different spellings used to live here — a wymix for tensors, an
    // XOR-multiply for scalars, and a bare wymix for the schema hash —
    // because wymix collapsed on a zero second operand and each site worked
    // around it separately.  fmix64 is a permutation and has no absorbing
    // value, so there is nothing left to work around and one spelling covers
    // the whole fold.  See the fmix64 doc-block in Expr.h.
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
    // Every producer sizes the scalar_args allocation to num_scalar_args, so the
    // loop over it is in bounds.  A null scalar_args contributes the count only.
    [[gnu::always_inline]] static void fold_entry_(uint64_t& content_hash_state, const TraceEntry& op_record) noexcept {
        content_hash_state = detail::combine_ids(content_hash_state, op_record.schema_hash.raw());

        for (uint16_t j = 0; j < op_record.num_inputs; j++) {
            const TensorMeta& input_meta = op_record.input_metas[j];
            const uint64_t per_dim_hash_xor = raw_dim_hash(detail::dim_hash_simd_det(input_meta));
            uint64_t meta_packed = static_cast<uint64_t>(std::to_underlying(input_meta.dtype))
                                 | (static_cast<uint64_t>(std::to_underlying(input_meta.device_type)) << 8)
                                 | (static_cast<uint64_t>(static_cast<uint8_t>(input_meta.device_idx)) << 16);
            content_hash_state = detail::combine_ids(content_hash_state, per_dim_hash_xor ^ meta_packed);
        }

        content_hash_state = detail::combine_ids(content_hash_state, static_cast<uint64_t>(op_record.num_scalar_args));
        if (op_record.scalar_args) {
            for (uint16_t s = 0; s < op_record.num_scalar_args; s++) {
                content_hash_state =
                    detail::combine_ids(content_hash_state, static_cast<uint64_t>(op_record.scalar_args[s]));
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
                                                                 const NumericalRecipe* recipe = nullptr) noexcept {
    CRUCIBLE_PRE(recipe == nullptr || ::foundation::decide::is_non_zero(recipe->hash));
    CRUCIBLE_PRE(recipe == nullptr || !recipe->hash.is_sentinel());
    ContentHashFold fold =
        (recipe == nullptr) ? ContentHashFold{ContentHashFold::NoRecipe{}} : ContentHashFold{*recipe};
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
            // the same content.  Mixing is fmix64 and XOR, which survives a
            // zero feedback or termination component: fmix64 is a permutation
            // and has no absorbing value.
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

// The ops array is stored, not copied: the caller keeps it alive for as long
// as the node.
//
// This overload hashes under no numerical recipe, which the recipe-taking
// overload further down is the counterpart to. The choice is visible in the
// signature the caller picks rather than in an argument the caller can leave
// out: a region hashed under the wrong numerics shares a kernel-cache slot
// with one hashed under the right ones, and the runtime would serve either
// kernel to either caller.
[[nodiscard]] inline RegionNode* make_region(::foundation::effects::Alloc a, Arena& arena CRUCIBLE_LIFETIMEBOUND,
                                             TraceEntry* ops, uint32_t num_ops) noexcept {
    CRUCIBLE_PRE(::foundation::decide::valid_span(num_ops, ops));
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
    CRUCIBLE_POST(node, ::foundation::decide::implies(num_ops > 0u, node->content_hash.raw() != 0));
    return node;
}

// For a caller that has already folded the hash while streaming the ops.
[[nodiscard]] inline RegionNode* make_region(::foundation::effects::Alloc a, Arena& arena CRUCIBLE_LIFETIMEBOUND,
                                             TraceEntry* ops, uint32_t num_ops, ContentHash precomputed_hash) noexcept {
    CRUCIBLE_PRE(::foundation::decide::valid_span(num_ops, ops));
    CRUCIBLE_PRE(::foundation::decide::is_non_zero(precomputed_hash) || num_ops == 0);
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
[[nodiscard]] inline RegionNode* make_region(::foundation::effects::Alloc a, Arena& arena CRUCIBLE_LIFETIMEBOUND,
                                             TraceEntry* ops, uint32_t num_ops,
                                             const NumericalRecipe* recipe) noexcept {
    CRUCIBLE_PRE(::foundation::decide::valid_span(num_ops, ops));
    CRUCIBLE_PRE(recipe != nullptr);
    CRUCIBLE_PRE(::foundation::decide::is_non_zero(recipe->hash));
    CRUCIBLE_PRE(!recipe->hash.is_sentinel());
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
    CRUCIBLE_POST(node, ::foundation::decide::implies(num_ops > 0u, node->content_hash.raw() != 0));
    return node;
}

[[nodiscard]] inline TraceNode* make_terminal(::foundation::effects::Alloc a, Arena& arena) noexcept {
    auto* node = new(arena.alloc_obj<TraceNode>(a)) TraceNode{};
    node->kind = TraceNodeKind::TERMINAL;
    CRUCIBLE_POST(node, node != nullptr);
    CRUCIBLE_POST(node, node->kind == TraceNodeKind::TERMINAL);
    return node;
}

// The body must be a complete sub-DAG ending in TERMINAL, and
// body_content_hash must already be folded from that body's region chain.
[[nodiscard]] inline LoopNode* make_loop(::foundation::effects::Alloc a, Arena& arena CRUCIBLE_LIFETIMEBOUND,
                                         TraceNode* body, ContentHash body_content_hash, FeedbackEdge* feedback,
                                         uint16_t num_feedback, LoopTermKind term_kind, uint32_t repeat_count,
                                         float epsilon = 0.0f) noexcept {
    CRUCIBLE_PRE(body != nullptr);
    CRUCIBLE_PRE(::foundation::decide::valid_span(num_feedback, feedback));
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
    // is the builtin, so this header does not include <cmath> for it.
    //
    // This is the third layer of CLAUDE.md §XII — an anonymous expression —
    // because neither layer above it reaches a float.  There is no Refined
    // predicate over floating point in the tree, and decide::non_negative is
    // constrained to std::integral, deliberately: on an integral type the
    // predicate has no NaN case to get wrong, which is the whole content of
    // this guard.  Lifting it is a one-name change once a second float
    // boundary wants the same test.
    CRUCIBLE_PRE(__builtin_isfinite(epsilon));
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
//
// The recipe is required rather than defaulted, and null is how a caller says
// it selects no numerics. The comparison below is between a hash folded here
// and a hash folded when the existing region was built, so the two have to be
// folded under the same recipe to be comparable at all. A defaulted parameter
// would let a caller that has a recipe omit it and get a silent stream of
// missed merges, every suffix looking distinct because the two sides folded
// different things.
[[nodiscard]] inline TraceNode* find_merge_point(std::span<TraceEntry> new_ops, TraceNode* existing_continuation,
                                                 const NumericalRecipe* recipe) {
    CRUCIBLE_PRE(recipe == nullptr || ::foundation::decide::is_non_zero(recipe->hash));
    CRUCIBLE_PRE(recipe == nullptr || !recipe->hash.is_sentinel());
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

        ContentHash new_hash =
            compute_content_hash(new_ops.subspan(new_pos - region->num_ops, region->num_ops), recipe);

        if (new_hash == region->content_hash) {
            merge = region;
            new_pos -= region->num_ops;
        } else {
            break;
        }
    }

    return merge;
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
                // UNTIL case of test_loop_replay_ignores_epsilon is what
                // reports it.
                //
                // An UNTIL count of zero cannot arrive: make_loop rejects it,
                // because it would skip the body and report success.
                CRUCIBLE_FATAL_INVARIANT(loop->term_kind != LoopTermKind::UNTIL || loop->repeat_count > 0u);
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
