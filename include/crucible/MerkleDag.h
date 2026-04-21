#pragma once

// Merkle DAG: Execute-and-record trace graph with content-addressable
// kernel caching, branch/merge detection, and DAG replay.
//
// Implements Crucible.md Part 2 (sections 2.1-2.9).
//
// Core types:
//   TraceEntry    — one recorded ATen op (shapes, dtypes, scalar args)
//   RegionNode    — compilable sequence of ops, linked to cached kernel
//   BranchNode    — guard point routing to different execution arms
//   KernelCache   — global lock-free content_hash -> CompiledKernel* map
//
// TraceRing and IterationDetector live in their own headers
// (TraceRing.h, IterationDetector.h) because they're also used by
// CrucibleContext and BackgroundThread without pulling in the full DAG.
//
// All nodes are arena-allocated.

#include <crucible/Arena.h>
#include <crucible/CKernel.h>
#include <crucible/Expr.h>
#include <crucible/IterationDetector.h>
#include <crucible/Reflect.h>
#include <crucible/TraceRing.h>
#include <crucible/safety/Refined.h>

#include <crucible/Types.h>

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <utility>

namespace crucible {

// Opaque compiled kernel handle (implemented by codegen backend)
struct CompiledKernel;

// ═══════════════════════════════════════════════════════════════════
// TensorMeta: Compact metadata for one tensor (no actual data)
//
// Sizes and strides inlined for up to 8 dimensions, covering 99.9%
// of real tensors. Arena-allocated when > 8 dims needed (via indirection
// at a higher level, not inside this struct).
// ═══════════════════════════════════════════════════════════════════

struct TensorMeta {
  int64_t sizes[8]{};        // 64B — zero-init prevents hash instability
  int64_t strides[8]{};      // 64B
  void* data_ptr = nullptr;  // 8B — tensor data pointer (for dataflow tracking)
  uint8_t ndim = 0;          // 1B — dimensions used (0-8)
  ScalarType dtype = ScalarType::Undefined; // 1B
  DeviceType device_type = DeviceType::CPU; // 1B
  int8_t device_idx = -1;   // 1B — -1 = CPU, 0+ = device index
  Layout layout = Layout::Strided; // 1B
  bool requires_grad = false; // 1B — tensor.requires_grad (forward/backward discriminator)

  // Packed tensor flags (1B). Bit layout:
  //   bit 0: is_leaf       — parameter or user-created tensor (no grad_fn)
  //   bit 1: is_contiguous — contiguous memory layout
  //   bit 2: has_grad_fn   — has autograd history (not a leaf)
  //   bit 3: is_view       — shares storage with another tensor
  //   bit 4: is_neg        — negation bit-view (torch._neg_view)
  //   bit 5: is_conj       — conjugate bit-view
  //   bit 6-7: reserved
  uint8_t flags = 0;          // 1B — packed tensor flags (see meta_flags)

  uint8_t output_nr = 0;     // 1B — autograd output number (multi-output ops)

  // ── Extended fields (24B) ─────────────────────────────────────────
  int64_t storage_offset = 0; // 8B — offset into underlying storage (view chains)
  uint32_t version = 0;      // 4B — tensor data version counter (in-place mutation detection)
  uint32_t storage_nbytes = 0; // 4B — actual storage size in bytes (may differ from view)
  uint64_t grad_fn_hash = 0;  // 8B — FNV-1a hash of grad_fn class name (e.g. "AddmmBackward0")
                               //      0 = no grad_fn (leaf tensor or no autograd)
};

static_assert(sizeof(TensorMeta) == 168, "TensorMeta layout check");
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE_STRICT(TensorMeta);

// TensorMeta::flags bit constants.
namespace meta_flags {
  inline constexpr uint8_t IS_LEAF       = 1 << 0;
  inline constexpr uint8_t IS_CONTIGUOUS = 1 << 1;
  inline constexpr uint8_t HAS_GRAD_FN  = 1 << 2;
  inline constexpr uint8_t IS_VIEW       = 1 << 3;
  inline constexpr uint8_t IS_NEG        = 1 << 4;
  inline constexpr uint8_t IS_CONJ       = 1 << 5;
} // namespace meta_flags

// ═══════════════════════════════════════════════════════════════════
// TensorSlot: One unique storage in a recorded iteration
//
// Identifies a distinct tensor storage, tracks its live range
// (birth_op..death_op), and holds the assigned offset within
// the pre-allocated memory pool.
// ═══════════════════════════════════════════════════════════════════

struct TensorSlot {
  uint64_t offset_bytes = 0;  // 8B — assigned position in the memory pool (supports >4GB)
  // ── 24B bulk-copyable region (matches SlotInfo layout) ──────────
  uint64_t nbytes = 0;        // 8B — storage size in bytes
  OpIndex birth_op;            // 4B — default = none (UINT32_MAX)
  OpIndex death_op;            // 4B — default = none (UINT32_MAX)
  ScalarType dtype = ScalarType::Undefined; // 1B
  DeviceType device_type = DeviceType::CPU; // 1B
  int8_t device_idx = -1;     // 1B
  Layout layout = Layout::Strided; // 1B
  bool is_external = false;   // 1B
  uint8_t pad[3]{};           // 3B
  // ── end bulk-copyable region ────────────────────────────────────
  SlotId slot_id;              // 4B — after bulk region for memcpy alignment
  uint8_t pad2[4]{};           // 4B — pad to 40B (implicit alignment pad, explicit for InitSafe)
};

static_assert(sizeof(TensorSlot) == 40, "TensorSlot must be 40 bytes");
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE_STRICT(TensorSlot);

// ═══════════════════════════════════════════════════════════════════
// MemoryPlan: Result of liveness analysis + offset assignment
//
// Maps each tensor slot to a byte offset within a single pool.
// External slots (params, data loader outputs) are excluded from
// the pool — they keep their existing allocations.
//
// Carries device and distributed context so each plan is
// self-describing — critical for heterogeneous multi-device
// execution where different devices get different compiled kernels
// and different memory layouts.
// ═══════════════════════════════════════════════════════════════════

struct MemoryPlan {
  TensorSlot* slots = nullptr; // 8B — arena-allocated array
  uint64_t pool_bytes = 0;     // 8B — total pool size needed
  uint32_t num_slots = 0;      // 4B — total unique storages
  uint32_t num_external = 0;   // 4B — how many are external (not in pool)

  // Device context: which device this plan targets.
  DeviceType device_type = DeviceType::CPU; // 1B
  int8_t device_idx = -1;     // 1B — device index
  uint8_t pad0[2]{};          // 2B — alignment
  uint64_t device_capability = 0; // 8B — SM version or equivalent (from CrucibleContext)

  // Distributed topology: for multi-device memory coordination.
  int32_t rank = -1;           // 4B — global rank (-1 = not distributed)
  int32_t world_size = 0;     // 4B — total processes (0 = not distributed)
};

// MemoryPlan layout: slots(8) + pool_bytes(8) + num_slots(4) + num_external(4) +
// device_type(1) + device_idx(1) + pad(2) + device_capability(8) + rank(4) +
// world_size(4) = 44B → aligned to 48 via trailing pad the compiler inserts.
static_assert(sizeof(MemoryPlan) == 48,
              "MemoryPlan size changed — update serializer and on-disk format");
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE_STRICT(MemoryPlan);

// Compute storage size in bytes from TensorMeta.
//
// The total storage span is the sum of per-dimension extents (not the
// max). For contiguous [3,4] strides [4,1]: (2*4)+(3*1)+1 = 12 elements.
// Handles negative strides (e.g. torch.flip, as_strided) by tracking
// both max and min offset contributions separately.
//
// Overflow handling: a corrupt or adversarial TensorMeta could carry
// huge sizes/strides whose product overflows int64_t silently — the
// pre-fix code computed (sizes-1)*strides without checking, leaving a
// path for downstream code to consume a wrapped-around byte count and
// either underallocate (use-after-free) or trip a contract.  Each
// arithmetic step is now overflow-checked via __builtin_*_overflow;
// any overflow returns UINT64_MAX so allocators downstream fail clean
// on "tensor too large" rather than silently wrap.
[[nodiscard]] constexpr uint64_t compute_storage_nbytes(const TensorMeta& m)
    pre (m.ndim <= 8)
{
  if (m.ndim == 0)
    return element_size(m.dtype);
  int64_t max_offset = 0;
  int64_t min_offset = 0;
  for (uint8_t d = 0; d < m.ndim; d++) {
    if (m.sizes[d] == 0) return 0; // zero-size tensor
    int64_t extent;
    // (sizes[d] - 1) * strides[d] can overflow for huge dims.
    // sizes[d] is positive, so the subtraction never overflows.
    if (__builtin_mul_overflow(m.sizes[d] - 1, m.strides[d], &extent)) [[unlikely]]
      return UINT64_MAX;
    if (extent > 0) {
      if (__builtin_add_overflow(max_offset, extent, &max_offset)) [[unlikely]]
        return UINT64_MAX;
    } else {
      if (__builtin_add_overflow(min_offset, extent, &min_offset)) [[unlikely]]
        return UINT64_MAX;
    }
  }
  // span = max_offset - min_offset + 1; both subtractions can overflow
  // when max and min straddle int64 limits.
  int64_t span_signed;
  if (__builtin_sub_overflow(max_offset, min_offset, &span_signed)) [[unlikely]]
    return UINT64_MAX;
  if (__builtin_add_overflow(span_signed, int64_t{1}, &span_signed)) [[unlikely]]
    return UINT64_MAX;
  // span is non-negative here (max >= 0 >= min, so max - min >= 0).
  uint64_t bytes;
  if (__builtin_mul_overflow(static_cast<uint64_t>(span_signed),
                             static_cast<uint64_t>(element_size(m.dtype)),
                             &bytes)) [[unlikely]]
    return UINT64_MAX;
  return bytes;
}

// ═══════════════════════════════════════════════════════════════════
// TraceEntry: One recorded ATen op
//
// Captures everything needed to reconstruct the op for compilation:
// op identity, input/output tensor metadata, scalar arguments,
// context flags, and tensor identity tracking for dataflow edges.
// All variable-length arrays are arena-allocated.
// ═══════════════════════════════════════════════════════════════════

struct TraceEntry {
  SchemaHash schema_hash;        // 8B — op identity (OperatorHandle schema hash)
  ShapeHash shape_hash;          // 8B — quick hash of all input shapes
  ScopeHash scope_hash;          // 8B — module hierarchy path hash (AST layer)
  CallsiteHash callsite_hash;    // 8B — Python source location identity

  TensorMeta* input_metas = nullptr;   // 8B — arena-allocated array
  TensorMeta* output_metas = nullptr;  // 8B — arena-allocated array
  uint16_t num_inputs = 0;      // 2B
  uint16_t num_outputs = 0;     // 2B

  int64_t* scalar_args = nullptr; // 8B — arena-allocated (null when num_scalar_args==0)
  uint16_t num_scalar_args = 0; // 2B

  bool grad_enabled = false;    // 1B — GradMode::is_enabled()
  bool inference_mode = false;  // 1B — InferenceMode active?

  // Op classification: populated by BackgroundThread during build_trace()
  // by calling classify_kernel(schema_hash). OPAQUE until the Vessel has
  // registered schema_hash → CKernelId mappings. Used by Tier 2+ replay
  // to dispatch directly without going through the Vessel dispatcher.
  CKernelId kernel_id = CKernelId::OPAQUE; // 1B
  bool is_mutable = false;    // 1B — schema.is_mutable (in-place or out= op)

  // Training phase: set by Python via TLS, unpacked from op_flags bits 2-3.
  // Distinguishes forward/backward/optimizer/other for L9-L11 intelligence.
  TrainingPhase training_phase = TrainingPhase::FORWARD; // 1B
  // __torch_function__ was active for this op (tensor subclass, dispatch mode).
  bool torch_function = false;  // 1B

  // Tensor identity tracking (for dataflow edges between ops)
  OpIndex* input_trace_indices = nullptr; // 8B — which previous op produced each input
  SlotId* input_slot_ids = nullptr;       // 8B — which pool slot each input reads from
  SlotId* output_slot_ids = nullptr;      // 8B — slot ID assigned to each output tensor

  // ── Span accessors (NullSafe: bounds-checked access to variable-length arrays) ──

  [[nodiscard]] std::span<const TensorMeta> input_span() const CRUCIBLE_LIFETIMEBOUND {
    return input_metas ? std::span{input_metas, num_inputs}
                       : std::span<const TensorMeta>{};
  }
  [[nodiscard]] std::span<TensorMeta> input_span() CRUCIBLE_LIFETIMEBOUND {
    return input_metas ? std::span{input_metas, num_inputs}
                       : std::span<TensorMeta>{};
  }
  [[nodiscard]] std::span<const TensorMeta> output_span() const CRUCIBLE_LIFETIMEBOUND {
    return output_metas ? std::span{output_metas, num_outputs}
                        : std::span<const TensorMeta>{};
  }
  [[nodiscard]] std::span<TensorMeta> output_span() CRUCIBLE_LIFETIMEBOUND {
    return output_metas ? std::span{output_metas, num_outputs}
                        : std::span<TensorMeta>{};
  }
  [[nodiscard]] std::span<const int64_t> scalar_span() const CRUCIBLE_LIFETIMEBOUND {
    return scalar_args ? std::span{scalar_args, num_scalar_args}
                       : std::span<const int64_t>{};
  }
  [[nodiscard]] std::span<const OpIndex> trace_index_span() const CRUCIBLE_LIFETIMEBOUND {
    return input_trace_indices ? std::span{input_trace_indices, num_inputs}
                               : std::span<const OpIndex>{};
  }
  [[nodiscard]] std::span<const SlotId> input_slot_span() const CRUCIBLE_LIFETIMEBOUND {
    return input_slot_ids ? std::span{input_slot_ids, num_inputs}
                          : std::span<const SlotId>{};
  }
  [[nodiscard]] std::span<const SlotId> output_slot_span() const CRUCIBLE_LIFETIMEBOUND {
    return output_slot_ids ? std::span{output_slot_ids, num_outputs}
                           : std::span<const SlotId>{};
  }
};

CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(TraceEntry);

// ═══════════════════════════════════════════════════════════════════
// Guard: Condition that determines branch selection at a BranchNode
// ═══════════════════════════════════════════════════════════════════

struct Guard {
  enum class Kind : uint8_t {
    SHAPE_DIM,     // A specific dimension of a specific input
    SCALAR_VALUE,  // A scalar produced by a specific op
    DTYPE,         // Input tensor dtype
    DEVICE,        // Input tensor device
    OP_SEQUENCE,   // The op at position N matches expected schema
  };

  Kind kind = Kind::SHAPE_DIM; // 1B
  uint8_t pad[3]{};    // 3B — alignment
  OpIndex op_index;    // 4B — default = none (UINT32_MAX)
  uint16_t arg_index = 0; // 2B — which argument (for SHAPE_DIM, DTYPE, DEVICE)
  uint16_t dim_index = 0; // 2B — which dimension (for SHAPE_DIM)

  // Reflection-driven: folds every non-static data member (kind, pad[3],
  // op_index.v, arg_index, dim_index) via fmix64.  Replaces the prior
  // manual bit-shift packing which dropped fields silently if Guard grew
  // a new field — a new discriminant on SHAPE_DIM / SCALAR_VALUE would
  // hash to the same value as the old one, making guards collide.
  // reflect_hash walks the struct at compile time: adding a field is a
  // hash-format break caught by CDAG_VERSION.
  //
  // gnu::pure: depends on *this fields only (memory through `this`).
  CRUCIBLE_PURE uint64_t hash() const noexcept {
    return crucible::reflect_hash(*this);
  }
};

static_assert(sizeof(Guard) == 12, "Guard must be 12 bytes");
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE_STRICT(Guard);

// ═══════════════════════════════════════════════════════════════════
// DAG Node Types
// ═══════════════════════════════════════════════════════════════════

enum class TraceNodeKind : uint8_t {
  REGION,    // A sequence of fusible ops -> compiled kernel
  BRANCH,    // A guard check -> routes to different arms
  LOOP,      // Cyclic computation: body × N or until convergence
  TERMINAL,  // End of trace
};

// Base node in the Merkle DAG. Arena-allocated, never freed individually.
struct TraceNode {
  TraceNodeKind kind{};     // 1B
  uint8_t pad[7]{};         // 7B — alignment for merkle_hash
  MerkleHash merkle_hash;   // 8B — subtree identity (includes all descendants)
                             // Field stays as MerkleHash (default-zero
                             // at construction, populated by
                             // recompute_merkle).  Type wrapping the
                             // field would break layout; consumers use
                             // the computed_merkle_hash() accessor
                             // below to get a non-zero-refined view.
  TraceNode* next = nullptr; // 8B — continuation (null for TERMINAL)

  // Accessor for callers that require merkle_hash to be populated.
  // Returns Refined<non_zero, MerkleHash> — caller must have called
  // recompute_merkle on this node (or its ancestor) before invoking,
  // or the Refined ctor's contract fires.
  //
  // Rationale: freshly-constructed nodes have merkle_hash == 0.
  // Comparing a fresh node's hash to a stored one gives a spurious
  // mismatch.  Routing through this accessor makes "the hash has been
  // computed" a load-bearing precondition at the type level.
  [[nodiscard]] crucible::safety::Refined<
      crucible::safety::non_zero, MerkleHash>
  computed_merkle_hash() const noexcept
      pre (merkle_hash.raw() != 0)
  {
    return crucible::safety::Refined<
        crucible::safety::non_zero, MerkleHash>{merkle_hash};
  }
};

static_assert(sizeof(TraceNode) == 24, "TraceNode must be 24 bytes");
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(TraceNode);

// ═══════════════════════════════════════════════════════════════════
// RegionNode: A compilable sequence of ops
//
// Contains the recorded ops and a pointer to the compiled kernel
// (if available). The compiled field is atomic because the background
// thread writes it and the foreground thread reads it.
// ═══════════════════════════════════════════════════════════════════

struct RegionNode : TraceNode {
  ContentHash content_hash;                       // 8B — kernel identity (this region)
  // NOT relaxed: publish pattern — bg writes compiled kernel data, then
  // stores pointer with release. Fg loads with acquire to see the data.
  std::atomic<CompiledKernel*> compiled{nullptr}; // 8B — from global cache, null until ready

  TraceEntry* ops = nullptr;  // 8B — arena-allocated array
  uint32_t num_ops = 0;      // 4B

  SchemaHash first_op_schema;   // 8B — quick mismatch detection

  float measured_ms = 0.0f;  // 4B — last measured execution time
  uint32_t variant_id = 0;   // 4B — which compiled variant is active

  MemoryPlan* plan = nullptr; // 8B — liveness analysis result (null until computed)
};

// RegionNode layout: TraceNode(24) + content_hash(8) + compiled(8) + ops(8)
// + num_ops(4) + pad(4) + first_op_schema(8) + measured_ms(4) + variant_id(4)
// + plan(8) = 80B.
static_assert(sizeof(RegionNode) == 80,
              "RegionNode size changed — update serializer, replay, content hash");

// ═══════════════════════════════════════════════════════════════════
// BranchNode: A guard point where execution can diverge
//
// Arms are value -> target pairs, SORTED by value for O(log n)
// binary search in replay(). The next field (from TraceNode)
// points to the merge point where all arms reconverge.
// ═══════════════════════════════════════════════════════════════════

struct BranchNode : TraceNode {
  Guard guard;              // 12B — what to check

  struct Arm {
    int64_t value = 0;      // 8B — the observed guard outcome
    TraceNode* target = nullptr; // 8B — the path for this outcome
  };

  Arm* arms = nullptr;      // 8B — arena-allocated array
  uint32_t num_arms = 0;    // 4B
  uint32_t pad1 = 0;        // 4B — alignment

  // Sortedness is a load-bearing invariant: the binary search in
  // replay() uses `arms[mid].value < val` to narrow to one arm.  If
  // arms are NOT sorted by value, the search returns wrong arms →
  // wrong routing → replay divergence against a region that otherwise
  // hashes identically.  The invariant is silent-failure until
  // divergence surfaces, often in production.
  //
  // This predicate confirms sortedness in O(num_arms) and is designed
  // for debug-only verification (gnu::cold): used by post-conditions
  // on arm-installation paths and by test assertions.  Adjacent-pair
  // comparison tolerates duplicate values (strict < would reject a
  // legitimate "two guards hash equal" case, which doesn't happen in
  // current code but isn't structurally forbidden).
  [[nodiscard, gnu::cold]] bool arms_sorted() const noexcept {
    for (uint32_t i = 1; i < num_arms; ++i)
      if (arms[i].value < arms[i - 1].value) return false;
    return true;
  }
};

// BranchNode layout locked: TraceNode(24) + Guard(~12B) padded + arms(8)
// + num_arms(4) + pad1(4).  Any layout change invalidates on-disk
// region hashes — the serializer reads this struct's bytes, so a size
// drift is a silent content-hash drift across Cipher round-trips.
static_assert(sizeof(BranchNode) == 56,
              "BranchNode size changed — update serializer + content hash");

// ═══════════════════════════════════════════════════════════════════
// FeedbackEdge: Body output → body input across loop iterations
//
// In a LoopNode, feedback edges carry data from the body's outputs
// back to its inputs for the next iteration. E.g., residual
// connections in transformer layers, hidden state in RNNs.
// ═══════════════════════════════════════════════════════════════════

struct FeedbackEdge {
  uint16_t output_idx = 0;  // 2B — which body output
  uint16_t input_idx = 0;   // 2B — which body input for next iteration
};

static_assert(sizeof(FeedbackEdge) == 4, "FeedbackEdge must be 4 bytes");
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(FeedbackEdge);

// ═══════════════════════════════════════════════════════════════════
// LoopNode: Cyclic computation within the acyclic Merkle DAG
//
// Wraps an acyclic body sub-DAG with feedback edges and termination.
// The body is a self-contained linked list of TraceNodes ending in
// TERMINAL. Feedback edges connect body outputs to body inputs for
// the next iteration. The LoopNode's `next` continues execution
// after all iterations complete.
//
// From the manifesto (L5/L6):
//   merkle_hash = hash(body.content_hash ⊕ "loop" ⊕ feedback_sig ⊕ termination)
//   Transforms DAG from computation snapshot to computation PROGRAM.
//
// 64 bytes = one cache line (matches GraphNode).
// ═══════════════════════════════════════════════════════════════════

enum class LoopTermKind : uint8_t {
  REPEAT,  // Fixed N iterations (RNN, fixed unrolling)
  UNTIL,   // Converge within epsilon (DEQ, diffusion denoising)
};

struct LoopNode : TraceNode {
  ContentHash body_content_hash;          // 8B — body sub-DAG content identity
  TraceNode* body = nullptr;              // 8B — body sub-DAG (ends with TERMINAL)
  FeedbackEdge* feedback_edges = nullptr; // 8B — arena-allocated
  uint16_t num_feedback = 0;             // 2B — feedback edge count
  LoopTermKind term_kind = LoopTermKind::REPEAT; // 1B
  uint8_t pad_l0 = 0;                    // 1B — InitSafe
  uint32_t repeat_count = 0;             // 4B — REPEAT: fixed N. UNTIL: observed iters.
  float epsilon = 0.0f;                  // 4B — convergence threshold (UNTIL only)
  float measured_body_ms = 0.0f;         // 4B — last measured body execution time

  [[nodiscard]] std::span<const FeedbackEdge> feedback_span() const CRUCIBLE_LIFETIMEBOUND {
    return feedback_edges ? std::span{feedback_edges, num_feedback}
                          : std::span<const FeedbackEdge>{};
  }
};

static_assert(sizeof(LoopNode) == 64, "LoopNode must be 64 bytes (one cache line)");
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(LoopNode);

// ═══════════════════════════════════════════════════════════════════
// LoopNode hash helpers
// ═══════════════════════════════════════════════════════════════════

// Fold feedback edges into a single signature via fmix64.
// Different feedback wiring → different signature.
// Uses fmix64 with nonzero seed (not wymix) because wymix(0, 0) = 0
// which collapses the chain when the first edge is {0,0}.
// Edge count is folded in so {A} ≠ {A, A}.
CRUCIBLE_PURE inline uint64_t feedback_signature(std::span<const FeedbackEdge> edges) noexcept {
  if (edges.empty()) return 0;
  constexpr uint64_t kSeed = 0x6665656462616B73ULL; // "feedbaks"
  uint64_t h = kSeed;
  for (const auto& e : edges) {
    uint64_t packed = (static_cast<uint64_t>(e.output_idx) << 16) | e.input_idx;
    h = detail::fmix64(h ^ packed);
  }
  h = detail::fmix64(h ^ edges.size());
  return h;
}

// Hash termination condition. Captures kind + repeat_count + epsilon bits.
// Uses fmix64 with salts (not wymix) to avoid the zero-input degenerate
// case: wymix(x, 0) = 0 for all x, which would collapse the hash chain.
CRUCIBLE_PURE inline uint64_t loopterm_hash(const LoopNode& ln) noexcept {
  constexpr uint64_t kTermSalt = 0x7465726D696E6174ULL; // "terminat"
  constexpr uint64_t kEpsSalt  = 0x65707369006C6F6EULL; // "epsi\0lon"
  uint64_t packed = static_cast<uint64_t>(std::to_underlying(ln.term_kind)) |
                    (static_cast<uint64_t>(ln.repeat_count) << 8);
  uint64_t h = detail::fmix64(packed ^ kTermSalt);
  h ^= detail::fmix64(static_cast<uint64_t>(std::bit_cast<uint32_t>(ln.epsilon)) ^ kEpsSalt);
  return h;
}

// Content hash of a body sub-DAG: wymix-fold content hashes of all body regions.
[[nodiscard]] inline ContentHash compute_body_content_hash(TraceNode* body) {
  uint64_t h = 0x9E3779B97F4A7C15ULL;
  TraceNode* walk = body;
  while (walk) {
    if (walk->kind == TraceNodeKind::REGION)
      h = detail::wymix(h, static_cast<RegionNode*>(walk)->content_hash.raw());
    walk = walk->next;
  }
  return ContentHash{detail::fmix64(h)};
}

// ═══════════════════════════════════════════════════════════════════
// Content and Merkle hash functions
//
// Content hash: identity of THIS node's ops only (cache key for kernels).
// Merkle hash: identity of the entire subtree from this node downward.
// Both use detail::fmix64 from Expr.h.
// ═══════════════════════════════════════════════════════════════════

[[nodiscard, gnu::pure]] inline ContentHash compute_content_hash(std::span<const TraceEntry> ops) noexcept {
  // XOR-fold content hash: for each tensor, fold all dimensions into a
  // single accumulator via independent multiplies (sizes[d] * kDimMix[d]),
  // then one wymix per tensor. Breaks the serial wymix-per-dimension
  // dependency chain: ndim XOR-folds (1 cy each, multiplies pipelined)
  // + 1 wymix (~5 cy) instead of ndim × wymix (~5 cy each, serial).
  // For ndim=4: ~13 cy vs ~22 cy per tensor (~40% faster).
  uint64_t h = 0x9E3779B97F4A7C15ULL;

  for (const auto& op : ops) {
    h = detail::wymix(h, op.schema_hash.raw());

    for (uint16_t j = 0; j < op.num_inputs; j++) {
      const TensorMeta& m = op.input_metas[j];
      uint64_t dim_h = 0;
      for (uint8_t d = 0; d < m.ndim; d++) {
        dim_h ^= static_cast<uint64_t>(m.sizes[d]) * detail::kDimMix[d];
        dim_h ^= static_cast<uint64_t>(m.strides[d]) * detail::kDimMix[d + 8];
      }
      uint64_t meta_packed =
          static_cast<uint64_t>(std::to_underlying(m.dtype)) |
          (static_cast<uint64_t>(std::to_underlying(m.device_type)) << 8) |
          (static_cast<uint64_t>(static_cast<uint8_t>(m.device_idx)) << 16);
      h = detail::wymix(h ^ dim_h, meta_packed);
    }

    if (op.scalar_args) {
      uint16_t n = std::min(op.num_scalar_args, uint16_t{5});
      for (uint16_t s = 0; s < n; s++) {
        h ^= static_cast<uint64_t>(op.scalar_args[s]);
        h *= 0x100000001b3ULL;
      }
    }
  }

  return ContentHash{detail::fmix64(h)};
}

[[nodiscard, gnu::pure]] inline MerkleHash compute_merkle_hash(TraceNode* node) noexcept {
  if (!node)
    return MerkleHash{};

  // Exhaustive switch: every TraceNodeKind arm must return (or fall
  // through to the tail).  A new kind added without an arm trips
  // std::unreachable rather than silently falling off the end with
  // uninitialized `h`.  Switches compile to jump tables; if/else-if
  // chains are opaque to that pattern.
  uint64_t h;
  switch (node->kind) {
    case TraceNodeKind::REGION:
      h = static_cast<RegionNode*>(node)->content_hash.raw();
      break;
    case TraceNodeKind::BRANCH: {
      auto* b = static_cast<BranchNode*>(node);
      h = detail::fmix64(b->guard.hash());
      for (uint32_t i = 0; i < b->num_arms; i++) {
        h ^= detail::fmix64(b->arms[i].target->merkle_hash.raw());
        h *= 0x9E3779B97F4A7C15ULL;
      }
      break;
    }
    case TraceNodeKind::LOOP: {
      auto* loop = static_cast<LoopNode*>(node);
      // Salt distinguishes LoopNode hashes from RegionNode hashes with
      // the same content. "LOOPNODE" in ASCII = 0x4C4F4F504E4F4445.
      // Use fmix64 + XOR (not wymix) to avoid zero-input degenerate case.
      // wymix(x, 0) = 0, which would collapse the entire hash chain when
      // feedback or termination components happen to be zero.
      constexpr uint64_t kLoopSalt = 0x4C4F4F504E4F4445ULL; // "LOOPNODE"
      constexpr uint64_t kFbSalt   = 0x6665656462616B00ULL;  // "feedbak\0"
      h = detail::fmix64(loop->body_content_hash.raw() ^ kLoopSalt);
      h ^= detail::fmix64(feedback_signature(loop->feedback_span()) ^ kFbSalt);
      h ^= loopterm_hash(*loop);
      break;
    }
    case TraceNodeKind::TERMINAL:
      return MerkleHash{};
    default:
      // All valid TraceNodeKind values handled above.  Any other value
      // is corrupt node memory and indicates a serious bug upstream.
      std::unreachable();
  }

  // Include continuation's merkle hash (this is what makes it Merkle)
  if (node->next)
    h = detail::fmix64(h ^ node->next->merkle_hash.raw());

  return MerkleHash{h};
}

// Content hash for a branched kernel (all arms fused into one kernel)
[[nodiscard]] inline ContentHash branched_content_hash(BranchNode* branch) {
  uint64_t h = detail::fmix64(branch->guard.hash());
  for (uint32_t i = 0; i < branch->num_arms; i++) {
    if (branch->arms[i].target->kind == TraceNodeKind::REGION) {
      h ^= detail::fmix64(
          static_cast<RegionNode*>(branch->arms[i].target)->content_hash.raw());
      h *= 0x9E3779B97F4A7C15ULL;
    }
  }
  return ContentHash{detail::fmix64(h)};
}

// ═══════════════════════════════════════════════════════════════════
// KernelCache: Global thread-safe content_hash -> CompiledKernel* map
//
// Open-addressing hash map. Lock-free reads via atomic pointers.
// Thread-safe inserts via CAS on the content_hash slot. Capacity
// must be a power of two.
// ═══════════════════════════════════════════════════════════════════

class CRUCIBLE_OWNER KernelCache {
 public:
  explicit KernelCache(uint32_t capacity = 4096) : capacity_(capacity) {
    assert((capacity & (capacity - 1)) == 0 && "capacity must be power of 2");
    table_ = static_cast<Entry*>(std::calloc(capacity_, sizeof(Entry)));
    if (!table_) [[unlikely]] std::abort(); // OOM is unrecoverable
    size_.store(0, std::memory_order_relaxed);
  }

  ~KernelCache() { std::free(table_); }

  KernelCache(const KernelCache&) = delete("lock-free hash map with atomic state cannot be copied");
  KernelCache& operator=(const KernelCache&) = delete("lock-free hash map with atomic state cannot be copied");
  KernelCache(KernelCache&&) = delete("lock-free hash map with atomic state cannot be moved");
  KernelCache& operator=(KernelCache&&) = delete("lock-free hash map with atomic state cannot be moved");

  // Lock-free lookup via atomic load. Any thread, safe by CAS protocol.
  // gnu::hot: called per dispatch_op in COMPILED mode (millions/sec).
  //
  // pre(content_hash.raw() != 0): the zero hash is the slot-empty
  // sentinel.  A lookup of the zero hash would linearly probe the
  // entire table (never match, never hit an empty slot) — an O(n)
  // path on the hot lookup function, which sees millions of calls
  // per second.  Legitimate callers never synthesize the zero hash;
  // a caller that did is always a bug.
  // [[assume]] propagates the fact to the optimizer: the probe's
  // `key == 0 -> miss` branch remains honest, but the input is
  // known-non-zero so downstream reasoning holds.
  CRUCIBLE_UNSAFE_BUFFER_USAGE
  [[nodiscard, gnu::hot]] CompiledKernel* lookup(ContentHash content_hash) const noexcept
      CRUCIBLE_NO_THREAD_SAFETY
      pre (content_hash.raw() != 0)
  {
    [[assume(content_hash.raw() != 0)]];
    uint32_t mask = capacity_ - 1;
    uint32_t idx = static_cast<uint32_t>(content_hash.raw()) & mask;
    for (uint32_t probe = 0; probe < capacity_; probe++) {
      auto& entry = table_[(idx + probe) & mask];
      uint64_t key = entry.content_hash.load(std::memory_order_acquire);
      if (key == content_hash.raw())
        return entry.kernel.load(std::memory_order_acquire);
      if (key == 0)
        return nullptr; // empty slot -> miss
    }
    return nullptr;
  }

  // Error channel for insert: callers that can tolerate a full table
  // can branch on it; callers that cannot must map it to crucible_abort.
  enum class InsertError : uint8_t {
    TableFull,  // Every slot probed was occupied by a different hash.
                // At load factor > 0.9 this is plausible on power-of-two
                // capacities; caller either (a) grows the cache offline
                // or (b) aborts.  Silent-discard was the pre-expected
                // behavior and hid capacity pressure.
  };

  // Thread-safe insert via CAS. Overwrites if key already exists.
  // Background thread primary writer, safe by atomic CAS protocol.
  //
  // Returns {} on success (including the update-existing-slot path).
  // Returns std::unexpected(TableFull) iff the entire probe chain was
  // full of foreign keys — no slot was free AND no match for this hash.
  // [[nodiscard]] forces callers to handle the error explicitly: the
  // previous silent-fail behavior made cache pressure invisible.
  CRUCIBLE_UNSAFE_BUFFER_USAGE
  [[nodiscard]] std::expected<void, InsertError>
  insert(ContentHash content_hash, CompiledKernel* kernel) CRUCIBLE_NO_THREAD_SAFETY
      pre (content_hash.raw() != 0)  // zero is the sentinel for empty slots
      pre (kernel != nullptr)
  {
    uint32_t mask = capacity_ - 1;
    uint32_t idx = static_cast<uint32_t>(content_hash.raw()) & mask;
    for (uint32_t probe = 0; probe < capacity_; probe++) {
      auto& entry = table_[(idx + probe) & mask];
      uint64_t expected = 0;
      if (entry.content_hash.compare_exchange_strong(
              expected, content_hash.raw(), std::memory_order_acq_rel)) {
        entry.kernel.store(kernel, std::memory_order_release);
        // Relaxed: size_ is informational only (no control flow depends
        // on its exact value). The real synchronization is content_hash CAS.
        size_.fetch_add(1, std::memory_order_relaxed);
        return {};
      }
      if (expected == content_hash.raw()) {
        // Already exists -- update to newer variant
        entry.kernel.store(kernel, std::memory_order_release);
        return {};
      }
    }
    return std::unexpected(InsertError::TableFull);
  }

  // Relaxed: informational counter, no ordering dependency.
  [[nodiscard]] uint32_t size() const CRUCIBLE_NO_THREAD_SAFETY {
    return size_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] uint32_t capacity() const { return capacity_; }

 private:
  struct Entry {
    // NOT relaxed: lock-free hash table protocol.
    // content_hash CAS(acq_rel) claims the slot; kernel.store(release)
    // publishes the compiled kernel. Readers load both with acquire
    // to see consistent (hash, kernel) pairs. Relaxed = reader sees
    // a hash match but loads a stale/null kernel pointer.
    std::atomic<uint64_t> content_hash{0};
    std::atomic<CompiledKernel*> kernel{nullptr};
  };

  Entry* table_;
  uint32_t capacity_;
  std::atomic<uint32_t> size_;
};

// TraceRing and IterationDetector are defined in their own headers
// (included above). They're also used standalone by CrucibleContext.h
// and BackgroundThread.h without needing the full DAG infrastructure.

// ═══════════════════════════════════════════════════════════════════
// Merkle DAG construction and traversal
// ═══════════════════════════════════════════════════════════════════

// Create a RegionNode from a span of TraceEntries.
// Placement-new with RegionNode{} zero-initializes via NSDMI.
// Only set the fields that differ from defaults.
[[nodiscard]] inline RegionNode* make_region(
    fx::Alloc a,
    Arena& arena CRUCIBLE_LIFETIMEBOUND,
    TraceEntry* ops,
    uint32_t num_ops) noexcept
    pre (num_ops == 0 || ops != nullptr)
{
  auto* node = new (arena.alloc_obj<RegionNode>(a))
      RegionNode{};
  node->kind = TraceNodeKind::REGION;
  node->ops = ops;
  node->num_ops = num_ops;
  node->content_hash = compute_content_hash(std::span{ops, num_ops});
  node->first_op_schema = (num_ops > 0) ? ops[0].schema_hash : SchemaHash{};
  return node;
}

// Overload: accept a pre-computed content hash (from build_trace's fused
// streaming hash). Eliminates the redundant second pass over all ops.
[[nodiscard]] inline RegionNode* make_region(
    fx::Alloc a,
    Arena& arena CRUCIBLE_LIFETIMEBOUND,
    TraceEntry* ops,
    uint32_t num_ops,
    ContentHash precomputed_hash) noexcept
    pre (num_ops == 0 || ops != nullptr)
{
  auto* node = new (arena.alloc_obj<RegionNode>(a))
      RegionNode{};
  node->kind = TraceNodeKind::REGION;
  node->ops = ops;
  node->num_ops = num_ops;
  node->content_hash = precomputed_hash;
  node->first_op_schema = (num_ops > 0) ? ops[0].schema_hash : SchemaHash{};
  return node;
}

// Create a terminal node. NSDMI handles zero-init; just set kind.
[[nodiscard]] inline TraceNode* make_terminal(fx::Alloc a, Arena& arena) noexcept {
  auto* node = new (arena.alloc_obj<TraceNode>(a))
      TraceNode{};
  node->kind = TraceNodeKind::TERMINAL;
  return node;
}

// Create a LoopNode. Body must be a complete sub-DAG (ending in TERMINAL).
// The body_content_hash is precomputed from the body's region chain.
[[nodiscard]] inline LoopNode* make_loop(
    fx::Alloc a,
    Arena& arena CRUCIBLE_LIFETIMEBOUND,
    TraceNode* body,
    ContentHash body_content_hash,
    FeedbackEdge* feedback,
    uint16_t num_feedback,
    LoopTermKind term_kind,
    uint32_t repeat_count,
    float epsilon = 0.0f) noexcept
    // Pointer / pair validity.
    pre (body != nullptr)
    pre (num_feedback == 0 || feedback != nullptr)
    // Semantic validity on the numeric parameters:
    //   epsilon must be non-negative.  Negative thresholds are
    //     meaningless (a convergence distance can't be negative), and
    //     the sign bit of epsilon flowing into loopterm_hash would
    //     produce two Merkle hashes for operationally-identical loops.
    //   REPEAT with repeat_count=0 is a legitimate degenerate case —
    //     used by replay() to test the "skip body entirely, run
    //     continuation" path.  Not enforced.
    //   UNTIL with epsilon=0 is permitted at construction (the caller
    //     may set it lazily).  Callers that actually run the loop are
    //     responsible for epsilon > 0 by the time the scheduler sees it.
    pre (!(epsilon < 0.0f))
{
  // pre() above replaces the runtime assert; kept as [[assume]] so the
  // optimizer can drop redundant null checks in the body.
  [[assume(body != nullptr)]];
  auto* node = new (arena.alloc_obj<LoopNode>(a))
      LoopNode{};
  node->kind = TraceNodeKind::LOOP;
  node->body = body;
  node->body_content_hash = body_content_hash;
  node->feedback_edges = feedback;
  node->num_feedback = num_feedback;
  node->term_kind = term_kind;
  node->repeat_count = repeat_count;
  node->epsilon = epsilon;
  return node;
}

// Recompute Merkle hashes bottom-up from a node.
// Assumes all descendants already have correct merkle_hash values.
inline void recompute_merkle(TraceNode* node) {
  if (!node)
    return;
  // Must recompute children first (bottom-up)
  if (node->kind == TraceNodeKind::BRANCH) {
    auto* b = static_cast<BranchNode*>(node);
    for (uint32_t i = 0; i < b->num_arms; i++)
      recompute_merkle(b->arms[i].target);
  } else if (node->kind == TraceNodeKind::LOOP) {
    recompute_merkle(static_cast<LoopNode*>(node)->body);
  }
  if (node->next)
    recompute_merkle(node->next);

  node->merkle_hash = compute_merkle_hash(node);
}

// Collect all RegionNodes into an output array. Returns count.
// Used to find uncompiled regions after DAG mutation.
[[nodiscard]] inline uint32_t collect_regions(
    TraceNode* node,
    std::span<RegionNode*> out,
    uint32_t count = 0) {
  while (node && count < out.size()) {
    switch (node->kind) {
      case TraceNodeKind::REGION:
        out[count++] = static_cast<RegionNode*>(node);
        break;
      case TraceNodeKind::BRANCH: {
        auto* b = static_cast<BranchNode*>(node);
        for (uint32_t i = 0; i < b->num_arms; i++)
          count = collect_regions(b->arms[i].target, out, count);
        break;
      }
      case TraceNodeKind::LOOP:
        count = collect_regions(static_cast<LoopNode*>(node)->body, out, count);
        break;
      case TraceNodeKind::TERMINAL:
        break;  // walk past; the next-pointer handles continuation
      default:
        std::unreachable();
    }
    node = node->next;
  }
  return count;
}

// ═══════════════════════════════════════════════════════════════════
// Merge detection: scan bottom-up for shared suffixes
//
// Walks the existing continuation and compares content hashes with
// new ops from the tail. Returns the first existing node where
// content matches (the merge point).
// ═══════════════════════════════════════════════════════════════════

[[nodiscard]] inline TraceNode* find_merge_point(
    std::span<TraceEntry> new_ops,
    TraceNode* existing_continuation) {
  constexpr uint32_t MAX_REGIONS = 1024;
  RegionNode* existing_regions[MAX_REGIONS];
  uint32_t num_existing = 0;

  TraceNode* walk = existing_continuation;
  while (walk && walk->kind == TraceNodeKind::REGION &&
         num_existing < MAX_REGIONS) {
    existing_regions[num_existing++] = static_cast<RegionNode*>(walk);
    walk = walk->next;
  }

  if (num_existing == 0)
    return nullptr;

  TraceNode* merge = nullptr;
  uint32_t new_pos = static_cast<uint32_t>(new_ops.size());
  uint32_t ex_idx = num_existing;

  while (ex_idx > 0 && new_pos > 0) {
    ex_idx--;
    RegionNode* region = existing_regions[ex_idx];
    if (new_pos < region->num_ops)
      break;

    ContentHash new_hash =
        compute_content_hash(new_ops.subspan(new_pos - region->num_ops, region->num_ops));

    if (new_hash == region->content_hash) {
      merge = region;
      new_pos -= region->num_ops;
    } else {
      break;
    }
  }

  return merge;
}

// ═══════════════════════════════════════════════════════════════════
// Branch creation: DAG mutation when divergence detected
//
// Splits a straight-line trace at the divergence point, creating a
// BranchNode with two arms (existing + new), merging where the
// continuations become identical.
// ═══════════════════════════════════════════════════════════════════

[[nodiscard]] inline BranchNode* add_branch(
    fx::Alloc a,
    Arena& arena,
    KernelCache& kernel_cache,
    TraceNode* divergence_point,
    TraceEntry* new_ops,
    uint32_t new_n,
    int64_t old_guard_value,
    int64_t new_guard_value,
    Guard guard,
    TraceNode* existing_suffix)
    pre (divergence_point != nullptr)
    pre (old_guard_value != new_guard_value)
{
  auto* new_region = make_region(a, arena, new_ops, new_n);

  TraceNode* merge = find_merge_point(std::span{new_ops, new_n}, existing_suffix);

  // 3. Wire new arm's tail to the merge point
  new_region->next = merge;

  // 4. Look up compiled kernel from global cache (may already exist)
  new_region->compiled.store(
      kernel_cache.lookup(new_region->content_hash),
      std::memory_order_release);

  // 5. Create BranchNode
  auto* branch = arena.alloc_obj<BranchNode>(a);
  ::new (branch) BranchNode{};
  branch->kind = TraceNodeKind::BRANCH;
  branch->guard = guard;
  branch->num_arms = 2;
  branch->arms = arena.alloc_array<BranchNode::Arm>(a, 2);
  // Keep arms sorted by value for O(log n) binary search in replay().
  if (old_guard_value <= new_guard_value) {
    branch->arms[0] = {.value = old_guard_value, .target = divergence_point};
    branch->arms[1] = {.value = new_guard_value, .target = new_region};
  } else {
    branch->arms[0] = {.value = new_guard_value, .target = new_region};
    branch->arms[1] = {.value = old_guard_value, .target = divergence_point};
  }
  branch->next = merge; // Shared continuation after merge

  // 6. Recompute Merkle hashes bottom-up
  recompute_merkle(branch);

  return branch;
}

// ═══════════════════════════════════════════════════════════════════
// Replay: Traverse the compiled Merkle DAG during execution.
//
// The live tensor pack and guard evaluator are provided as callbacks.
// This function is called from the foreground thread during replay
// mode (after the DAG is compiled).
//
// GuardEval signature: int64_t(const Guard&)
// RegionExec signature: void(RegionNode*)
// ═══════════════════════════════════════════════════════════════════

template <typename GuardEval, typename RegionExec>
[[nodiscard, gnu::flatten]] inline bool replay(
    TraceNode* node,
    GuardEval&& eval_guard,
    RegionExec&& exec_region) {
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

      // Binary search depends on arms[i].value being ascending.  In
      // debug, assert the invariant on first entry to catch builders
      // that populated arms in wrong order — silent failure otherwise
      // (wrong arm selected, replay diverges against a region that
      // hashes identical to the correct one).  Zero-cost under NDEBUG
      // since arms_sorted is gnu::cold and contract_assert compiles
      // out.
      contract_assert(branch->arms_sorted());

      // Binary search on sorted arms. Arms are kept sorted by value
      // at creation time (add_branch). O(log n) for large arm counts,
      // degenerates to 1-2 comparisons for the common 2-3 arm case.
      TraceNode* arm = nullptr;
      {
        uint32_t lo = 0, hi = branch->num_arms;
        while (lo < hi) {
          uint32_t mid = lo + (hi - lo) / 2;
          // Binary-search invariant: mid ∈ [lo, hi) and hi ≤ num_arms.
          // Propagate to the optimizer so arms[mid] drops the bounds
          // check GCC would emit under -D_GLIBCXX_ASSERTIONS.
          [[assume(mid < branch->num_arms)]];
          int64_t mid_val = branch->arms[mid].value;
          if (mid_val < val) lo = mid + 1;
          else if (mid_val > val) hi = mid;
          else { arm = branch->arms[mid].target; break; }
        }
      }

      if (!arm)
        return false; // Unseen guard value -- fall back to recording

      // Execute the arm's branch-specific regions
      if (!replay(arm, eval_guard, exec_region))
        return false;

      // Continue from the merge point (shared suffix)
      node = branch->next;
      break;
    }

    case TraceNodeKind::LOOP: {
      auto* loop = static_cast<LoopNode*>(node);
      assert(loop->body && "LoopNode body must be non-null");
      for (uint32_t i = 0; i < loop->repeat_count; i++) {
        if (!replay(loop->body, eval_guard, exec_region))
          return false;
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

// ═══════════════════════════════════════════════════════════════════
// DAG Diff: O(log N) structural comparison via Merkle hashes
//
// Compares two Merkle DAGs and finds the first divergent node.
// Merkle hashes enable massive pruning: identical subtrees (same
// merkle_hash) are skipped in O(1). Only divergent paths are
// explored, giving O(log N) for typical "one region changed"
// scenarios. Worst case O(N) when everything differs.
//
// This is the "git diff" for computation graphs. Used for:
//   - Version comparison (which regions changed between iterations?)
//   - Regression bisection (binary search through DAG versions)
//   - Deployment validation (new model == old model where expected?)
// ═══════════════════════════════════════════════════════════════════

struct DagDiff {
  TraceNode* node_a = nullptr;
  TraceNode* node_b = nullptr;
  uint32_t depth = 0;

  enum class Kind : uint8_t {
    IDENTICAL,         // Merkle hashes match — subtrees are equal
    KIND_MISMATCH,     // Different node kinds at this position
    CONTENT_MISMATCH,  // Same kind but different content (different ops)
    BRANCH_MISMATCH,   // Different guard or arm structure
    LOOP_MISMATCH,     // Different body, feedback, or termination
    STRUCTURE_MISMATCH,// One terminates, other continues (length differs)
  } kind = Kind::IDENTICAL;
};

// Walk two DAGs in lockstep, pruning identical subtrees via Merkle hash.
// Iterative on the next-chain (no stack overflow for long traces),
// recursive only into branch arms and loop bodies (bounded depth).
[[nodiscard]] inline DagDiff dag_diff(TraceNode* a, TraceNode* b, uint32_t depth = 0) {
  while (a && b) {
    // Merkle match → entire subtree from here is identical → skip
    if (a->merkle_hash == b->merkle_hash)
      return {nullptr, nullptr, depth, DagDiff::Kind::IDENTICAL};

    // Kind mismatch is a hard divergence
    if (a->kind != b->kind)
      return {a, b, depth, DagDiff::Kind::KIND_MISMATCH};

    if (a->kind == TraceNodeKind::TERMINAL)
      return {nullptr, nullptr, depth, DagDiff::Kind::IDENTICAL};

    if (a->kind == TraceNodeKind::REGION) {
      auto* ra = static_cast<RegionNode*>(a);
      auto* rb = static_cast<RegionNode*>(b);
      if (ra->content_hash != rb->content_hash)
        return {a, b, depth, DagDiff::Kind::CONTENT_MISMATCH};
      // Content matches — divergence is in the tail
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
      // Check arms recursively (bounded by branch fan-out)
      for (uint32_t i = 0; i < ba->num_arms; i++) {
        if (ba->arms[i].value != bb->arms[i].value)
          return {a, b, depth, DagDiff::Kind::BRANCH_MISMATCH};
        DagDiff arm_diff = dag_diff(ba->arms[i].target, bb->arms[i].target, depth + 1);
        if (arm_diff.kind != DagDiff::Kind::IDENTICAL)
          return arm_diff;
      }
      a = a->next;
      b = b->next;
      ++depth;
      continue;
    }

    if (a->kind == TraceNodeKind::LOOP) {
      auto* la = static_cast<LoopNode*>(a);
      auto* lb = static_cast<LoopNode*>(b);
      if (la->body_content_hash != lb->body_content_hash ||
          la->num_feedback != lb->num_feedback ||
          la->term_kind != lb->term_kind ||
          la->repeat_count != lb->repeat_count ||
          std::bit_cast<uint32_t>(la->epsilon)
              != std::bit_cast<uint32_t>(lb->epsilon))
        return {a, b, depth, DagDiff::Kind::LOOP_MISMATCH};
      // Feedback edges differ?
      if (la->num_feedback > 0 &&
          std::memcmp(la->feedback_edges, lb->feedback_edges,
                      la->num_feedback * sizeof(FeedbackEdge)) != 0)
        return {a, b, depth, DagDiff::Kind::LOOP_MISMATCH};
      // Body identical at content level; check sub-structure
      DagDiff body_diff = dag_diff(la->body, lb->body, depth + 1);
      if (body_diff.kind != DagDiff::Kind::IDENTICAL)
        return body_diff;
      a = a->next;
      b = b->next;
      ++depth;
      continue;
    }

    // Unknown kind — should not happen
    return {a, b, depth, DagDiff::Kind::KIND_MISMATCH};
  }

  // One or both ended
  if (a == b) // both null
    return {nullptr, nullptr, depth, DagDiff::Kind::IDENTICAL};
  return {a, b, depth, DagDiff::Kind::STRUCTURE_MISMATCH};
}

// Count nodes in a DAG (follows next chain + recurse into branches/loops).
// Useful for determining N for complexity analysis of dag_diff.
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
        break;  // leaf-like: already counted via ++count
      default:
        std::unreachable();
    }
    node = node->next;
  }
  return count;
}

} // namespace crucible
