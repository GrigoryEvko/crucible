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
#include <crucible/TraceRing.h>

#include <crucible/Types.h>

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
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
  uint8_t pad[3]{};          // 3B — padding to 8-byte alignment
};

static_assert(sizeof(TensorMeta) == 144, "TensorMeta layout check");

// ═══════════════════════════════════════════════════════════════════
// TensorSlot: One unique storage in a recorded iteration
//
// Identifies a distinct tensor storage, tracks its live range
// (birth_op..death_op), and holds the assigned offset within
// the pre-allocated memory pool.
// ═══════════════════════════════════════════════════════════════════

struct TensorSlot {
  uint64_t offset_bytes = 0;  // 8B — assigned position in the memory pool (supports >4GB)
  uint64_t nbytes = 0;        // 8B — storage size in bytes
  SlotId slot_id;              // 4B — default = none (UINT32_MAX)
  OpIndex birth_op;            // 4B — default = none (UINT32_MAX)
  OpIndex death_op;            // 4B — default = none (UINT32_MAX)
  ScalarType dtype = ScalarType::Undefined; // 1B
  DeviceType device_type = DeviceType::CPU; // 1B
  int8_t device_idx = -1;     // 1B
  Layout layout = Layout::Strided; // 1B
  bool is_external = false;   // 1B
  uint8_t pad[3]{};           // 3B — pad to 40B
};

static_assert(sizeof(TensorSlot) == 40, "TensorSlot must be 40 bytes");

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

// Compute storage size in bytes from TensorMeta (max extent * element size).
[[nodiscard]] constexpr uint64_t compute_storage_nbytes(const TensorMeta& m) {
  if (m.ndim == 0)
    return element_size(m.dtype);
  int64_t max_offset = 0;
  for (uint8_t d = 0; d < m.ndim; d++) {
    if (m.sizes[d] > 0)
      max_offset = std::max(max_offset, (m.sizes[d] - 1) * m.strides[d]);
  }
  return static_cast<uint64_t>(max_offset + 1) * element_size(m.dtype);
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
  uint64_t schema_hash = 0;     // 8B — op identity (OperatorHandle schema hash)
  uint64_t shape_hash = 0;      // 8B — quick hash of all input shapes
  uint64_t scope_hash = 0;      // 8B — module hierarchy path hash (AST layer)
  uint64_t callsite_hash = 0;   // 8B — Python source location identity

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
  uint8_t   pad_te = 0;        // 1B — alignment

  // Tensor identity tracking (for dataflow edges between ops)
  OpIndex* input_trace_indices = nullptr; // 8B — which previous op produced each input
  SlotId* input_slot_ids = nullptr;       // 8B — which pool slot each input reads from
  SlotId* output_slot_ids = nullptr;      // 8B — slot ID assigned to each output tensor

  // ── Span accessors (NullSafe: bounds-checked access to variable-length arrays) ──

  [[nodiscard]] std::span<const TensorMeta> input_span() const {
    return input_metas ? std::span{input_metas, num_inputs}
                       : std::span<const TensorMeta>{};
  }
  [[nodiscard]] std::span<TensorMeta> input_span() {
    return input_metas ? std::span{input_metas, num_inputs}
                       : std::span<TensorMeta>{};
  }
  [[nodiscard]] std::span<const TensorMeta> output_span() const {
    return output_metas ? std::span{output_metas, num_outputs}
                        : std::span<const TensorMeta>{};
  }
  [[nodiscard]] std::span<TensorMeta> output_span() {
    return output_metas ? std::span{output_metas, num_outputs}
                        : std::span<TensorMeta>{};
  }
  [[nodiscard]] std::span<const int64_t> scalar_span() const {
    return scalar_args ? std::span{scalar_args, num_scalar_args}
                       : std::span<const int64_t>{};
  }
  [[nodiscard]] std::span<const OpIndex> trace_index_span() const {
    return input_trace_indices ? std::span{input_trace_indices, num_inputs}
                               : std::span<const OpIndex>{};
  }
  [[nodiscard]] std::span<const SlotId> input_slot_span() const {
    return input_slot_ids ? std::span{input_slot_ids, num_inputs}
                          : std::span<const SlotId>{};
  }
  [[nodiscard]] std::span<const SlotId> output_slot_span() const {
    return output_slot_ids ? std::span{output_slot_ids, num_outputs}
                           : std::span<const SlotId>{};
  }
};

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

  // Note: arg_index and dim_index are included in the hash for ALL guard
  // kinds, so they MUST be initialized even when semantically unused.
  uint64_t hash() const {
    return detail::fmix64(
        std::to_underlying(kind) |
        (static_cast<uint64_t>(op_index.raw()) << 8) |
        (static_cast<uint64_t>(arg_index) << 40) |
        (static_cast<uint64_t>(dim_index) << 56));
  }
};

static_assert(sizeof(Guard) == 12, "Guard must be 12 bytes");

// ═══════════════════════════════════════════════════════════════════
// DAG Node Types
// ═══════════════════════════════════════════════════════════════════

enum class TraceNodeKind : uint8_t {
  REGION,    // A sequence of fusible ops -> compiled kernel
  BRANCH,    // A guard check -> routes to different arms
  TERMINAL,  // End of trace
};

// Base node in the Merkle DAG. Arena-allocated, never freed individually.
struct TraceNode {
  TraceNodeKind kind;     // 1B
  uint8_t pad[7];         // 7B — alignment for merkle_hash
  uint64_t merkle_hash;   // 8B — subtree identity (includes all descendants)
  TraceNode* next;        // 8B — continuation (null for TERMINAL)
};

static_assert(sizeof(TraceNode) == 24, "TraceNode must be 24 bytes");

// ═══════════════════════════════════════════════════════════════════
// RegionNode: A compilable sequence of ops
//
// Contains the recorded ops and a pointer to the compiled kernel
// (if available). The compiled field is atomic because the background
// thread writes it and the foreground thread reads it.
// ═══════════════════════════════════════════════════════════════════

struct RegionNode : TraceNode {
  uint64_t content_hash;                  // 8B — kernel identity (this region)
  std::atomic<CompiledKernel*> compiled;  // 8B — from global cache, null until ready

  TraceEntry* ops;          // 8B — arena-allocated array
  uint32_t num_ops;         // 4B

  uint64_t first_op_schema; // 8B — quick mismatch detection

  float measured_ms;        // 4B — last measured execution time
  uint32_t variant_id;      // 4B — which compiled variant is active

  MemoryPlan* plan;         // 8B — liveness analysis result (null until computed)
};

// ═══════════════════════════════════════════════════════════════════
// BranchNode: A guard point where execution can diverge
//
// Arms are value -> target pairs. The next field (from TraceNode)
// points to the merge point where all arms reconverge.
// ═══════════════════════════════════════════════════════════════════

struct BranchNode : TraceNode {
  Guard guard;              // 12B — what to check

  struct Arm {
    int64_t value;          // 8B — the observed guard outcome
    TraceNode* target;      // 8B — the path for this outcome
  };

  Arm* arms;                // 8B — arena-allocated array
  uint32_t num_arms;        // 4B
  uint32_t pad1;            // 4B — alignment
};

// ═══════════════════════════════════════════════════════════════════
// Content and Merkle hash functions
//
// Content hash: identity of THIS node's ops only (cache key for kernels).
// Merkle hash: identity of the entire subtree from this node downward.
// Both use detail::fmix64 from Expr.h.
// ═══════════════════════════════════════════════════════════════════

[[nodiscard]] inline uint64_t compute_content_hash(std::span<const TraceEntry> ops) {
  uint64_t h = 0x9E3779B97F4A7C15ULL;
  for (uint32_t i = 0; i < ops.size(); i++) {
    h ^= detail::fmix64(ops[i].schema_hash);
    h *= 0x9E3779B97F4A7C15ULL;

    // Input shapes + strides + dtype + device contribute to content identity.
    for (uint16_t j = 0; j < ops[i].num_inputs; j++) {
      const TensorMeta& m = ops[i].input_metas[j];
      for (uint8_t d = 0; d < m.ndim; d++) {
        h ^= detail::fmix64(static_cast<uint64_t>(m.sizes[d]));
        h *= 0x100000001b3ULL; // FNV prime
      }
      // Strides determine memory layout (contiguous vs transposed vs channels_last).
      for (uint8_t d = 0; d < m.ndim; d++) {
        h ^= detail::fmix64(static_cast<uint64_t>(m.strides[d]));
        h *= 0x100000001b3ULL;
      }
      // Dtype + device_type + device_idx
      h ^= detail::fmix64(
          static_cast<uint64_t>(static_cast<uint8_t>(m.dtype)) |
          (static_cast<uint64_t>(static_cast<uint8_t>(m.device_type)) << 8) |
          (static_cast<uint64_t>(static_cast<uint8_t>(m.device_idx)) << 16));
    }

    // Scalar args differentiate e.g. add(x,y,alpha=0.5) from add(x,y,alpha=2.0).
    if (ops[i].scalar_args) {
      uint16_t n_scalars = std::min(ops[i].num_scalar_args, uint16_t(5));
      for (uint16_t s = 0; s < n_scalars; s++) {
        h ^= detail::fmix64(static_cast<uint64_t>(ops[i].scalar_args[s]));
        h *= 0x100000001b3ULL;
      }
    }
  }
  return detail::fmix64(h);
}

[[nodiscard]] inline uint64_t compute_merkle_hash(TraceNode* node) {
  if (!node)
    return 0;

  uint64_t h;
  if (node->kind == TraceNodeKind::REGION) {
    h = static_cast<RegionNode*>(node)->content_hash;
  } else if (node->kind == TraceNodeKind::BRANCH) {
    auto* b = static_cast<BranchNode*>(node);
    h = detail::fmix64(b->guard.hash());
    for (uint32_t i = 0; i < b->num_arms; i++) {
      h ^= detail::fmix64(b->arms[i].target->merkle_hash);
      h *= 0x9E3779B97F4A7C15ULL;
    }
  } else {
    // TERMINAL
    return 0;
  }

  // Include continuation's merkle hash (this is what makes it Merkle)
  if (node->next)
    h = detail::fmix64(h ^ node->next->merkle_hash);

  return h;
}

// Content hash for a branched kernel (all arms fused into one kernel)
[[nodiscard]] inline uint64_t branched_content_hash(BranchNode* branch) {
  uint64_t h = detail::fmix64(branch->guard.hash());
  for (uint32_t i = 0; i < branch->num_arms; i++) {
    if (branch->arms[i].target->kind == TraceNodeKind::REGION) {
      h ^= detail::fmix64(
          static_cast<RegionNode*>(branch->arms[i].target)->content_hash);
      h *= 0x9E3779B97F4A7C15ULL;
    }
  }
  return detail::fmix64(h);
}

// ═══════════════════════════════════════════════════════════════════
// KernelCache: Global thread-safe content_hash -> CompiledKernel* map
//
// Open-addressing hash map. Lock-free reads via atomic pointers.
// Thread-safe inserts via CAS on the content_hash slot. Capacity
// must be a power of two.
// ═══════════════════════════════════════════════════════════════════

class KernelCache {
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

  // Lock-free lookup. Returns nullptr on miss.
  [[nodiscard]] CompiledKernel* lookup(uint64_t content_hash) const {
    uint32_t mask = capacity_ - 1;
    uint32_t idx = static_cast<uint32_t>(content_hash) & mask;
    for (uint32_t probe = 0; probe < capacity_; probe++) {
      auto& entry = table_[(idx + probe) & mask];
      uint64_t key = entry.content_hash.load(std::memory_order_acquire);
      if (key == content_hash)
        return entry.kernel.load(std::memory_order_acquire);
      if (key == 0)
        return nullptr; // empty slot -> miss
    }
    return nullptr;
  }

  // Thread-safe insert via CAS. Overwrites if key already exists.
  void insert(uint64_t content_hash, CompiledKernel* kernel) {
    assert(content_hash != 0 && "zero is the sentinel for empty slots");
    uint32_t mask = capacity_ - 1;
    uint32_t idx = static_cast<uint32_t>(content_hash) & mask;
    for (uint32_t probe = 0; probe < capacity_; probe++) {
      auto& entry = table_[(idx + probe) & mask];
      uint64_t expected = 0;
      if (entry.content_hash.compare_exchange_strong(
              expected, content_hash, std::memory_order_acq_rel)) {
        entry.kernel.store(kernel, std::memory_order_release);
        size_.fetch_add(1, std::memory_order_relaxed);
        return;
      }
      if (expected == content_hash) {
        // Already exists -- update to newer variant
        entry.kernel.store(kernel, std::memory_order_release);
        return;
      }
    }
    // Table full -- should not happen with proper sizing
    assert(false && "KernelCache table full");
  }

  [[nodiscard]] uint32_t size() const { return size_.load(std::memory_order_relaxed); }
  [[nodiscard]] uint32_t capacity() const { return capacity_; }

 private:
  struct Entry {
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
// Cannot memset because std::atomic is non-trivially-copyable;
// each field is initialized explicitly.
[[nodiscard]] inline RegionNode* make_region(
    Arena& arena,
    TraceEntry* ops,
    uint32_t num_ops) {
  auto* node = new (arena.alloc(sizeof(RegionNode), alignof(RegionNode)))
      RegionNode{};
  node->kind = TraceNodeKind::REGION;
  node->merkle_hash = 0;
  node->next = nullptr;
  node->ops = ops;
  node->num_ops = num_ops;
  node->content_hash = compute_content_hash(std::span{ops, num_ops});
  node->first_op_schema = (num_ops > 0) ? ops[0].schema_hash : 0;
  node->compiled.store(nullptr, std::memory_order_relaxed);
  node->measured_ms = 0.0f;
  node->variant_id = 0;
  node->plan = nullptr;
  return node;
}

// Create a terminal node.
[[nodiscard]] inline TraceNode* make_terminal(Arena& arena) {
  auto* node = arena.alloc_obj<TraceNode>();
  std::memset(node, 0, sizeof(TraceNode));
  node->kind = TraceNodeKind::TERMINAL;
  node->next = nullptr;
  node->merkle_hash = 0;
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
    if (node->kind == TraceNodeKind::REGION) {
      out[count++] = static_cast<RegionNode*>(node);
    } else if (node->kind == TraceNodeKind::BRANCH) {
      auto* b = static_cast<BranchNode*>(node);
      for (uint32_t i = 0; i < b->num_arms; i++)
        count = collect_regions(b->arms[i].target, out, count);
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

    uint64_t new_hash =
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
    Arena& arena,
    KernelCache& kernel_cache,
    TraceNode* divergence_point,
    TraceEntry* new_ops,
    uint32_t new_n,
    int64_t old_guard_value,
    int64_t new_guard_value,
    Guard guard,
    TraceNode* existing_suffix) {
  auto* new_region = make_region(arena, new_ops, new_n);

  TraceNode* merge = find_merge_point(std::span{new_ops, new_n}, existing_suffix);

  // 3. Wire new arm's tail to the merge point
  new_region->next = merge;

  // 4. Look up compiled kernel from global cache (may already exist)
  new_region->compiled.store(
      kernel_cache.lookup(new_region->content_hash),
      std::memory_order_relaxed);

  // 5. Create BranchNode
  auto* branch = arena.alloc_obj<BranchNode>();
  std::memset(branch, 0, sizeof(BranchNode));
  branch->kind = TraceNodeKind::BRANCH;
  branch->guard = guard;
  branch->num_arms = 2;
  branch->arms = arena.alloc_array<BranchNode::Arm>(2);
  branch->arms[0] = {old_guard_value, divergence_point};
  branch->arms[1] = {new_guard_value, new_region};
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
[[nodiscard]] inline bool replay(
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

      // Find the matching arm
      TraceNode* arm = nullptr;
      for (uint32_t i = 0; i < branch->num_arms; i++) {
        if (branch->arms[i].value == val) {
          arm = branch->arms[i].target;
          break;
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

    case TraceNodeKind::TERMINAL:
      return true;
    }
  }
  return true;
}

} // namespace crucible
