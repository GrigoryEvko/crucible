#pragma once

// Graph IR (Layer 2): Mutable computation graph for kernel scheduling.
//
// Replaces inductor's ir.py with three compact C++ types:
//   GraphNode (64B) — one operation producing output buffer(s)
//   Inst (8B)       — one micro-op in SSA form (kernel body)
//   Graph           — arena-owned container with transforms
//
// Python closure-based inner_fn becomes an explicit micro-op DAG
// (ComputeBody) that is inspectable, serializable, and directly
// emittable to CUDA C++.

#include <crucible/Arena.h>
#include <crucible/CKernel.h>
#include <crucible/Expr.h>
#include <crucible/Platform.h>

#include <cassert>
#include <cstdint>
#include <cstring>
#include <span>

namespace crucible {

// Forward declarations (Graph stores pointers, not objects)
class ExprPool;
class SymbolTable;

// ═══════════════════════════════════════════════════════════════════
// Node classification
// ═══════════════════════════════════════════════════════════════════

enum class NodeKind : uint8_t {
  INPUT,      // Graph input (no computation)
  CONSTANT,   // Compile-time constant tensor
  POINTWISE,  // Element-wise computation
  REDUCTION,  // Reduction (sum, max, argmax, etc.)
  SCAN,       // Prefix scan (cumsum, cumprod)
  SORT,       // Sort operation
  EXTERN,     // Opaque external kernel (mm, conv, cuBLAS)
  TEMPLATE,   // Template-based kernel (CUTLASS, Triton)
  MUTATION,   // In-place mutation of existing buffer
  NOP,        // No computation (concat, view, etc.)
};

enum class ReduceOp : uint8_t {
  SUM, PROD, MAX, MIN, ARGMAX, ARGMIN, ANY, XOR_SUM, WELFORD, DOT,
};

enum class ReduceHint : uint8_t { DEFAULT, INNER, OUTER };

struct NodeFlags {
  static constexpr uint8_t DEAD     = 1 << 0;
  static constexpr uint8_t VISITED  = 1 << 1;
  static constexpr uint8_t FUSED    = 1 << 2;
  static constexpr uint8_t REALIZED = 1 << 3;
};

// ═══════════════════════════════════════════════════════════════════
// CKernelId → NodeKind classification
//
// Maps the 146-op CKernel taxonomy to Graph IR node kinds.
// Used by the lowering pass from TraceEntry to Graph.
// ═══════════════════════════════════════════════════════════════════

[[nodiscard]] inline NodeKind classify_node_kind(CKernelId kid) {
  // Specific overrides before range checks.
  if (kid == CKernelId::REDUCE_CUMSUM || kid == CKernelId::ASSOC_SCAN)
    return NodeKind::SCAN;
  if (kid == CKernelId::COPY_)
    return NodeKind::MUTATION;

  // Activations + all elementwise (ACT_RELU..EWISE_FILL) → POINTWISE.
  // These ranges are contiguous in the CKernelId enum.
  if (kid >= CKernelId::ACT_RELU && kid <= CKernelId::EWISE_FILL)
    return NodeKind::POINTWISE;

  // Reductions (SUM, MEAN, MAX, MIN, ARGMAX, ARGMIN, TOPK) → REDUCTION.
  // REDUCE_CUMSUM already handled above as SCAN.
  if (kid >= CKernelId::REDUCE_SUM && kid <= CKernelId::REDUCE_TOPK)
    return NodeKind::REDUCTION;

  // Data movement (VIEW..UNFOLD) → NOP (no computation, metadata only).
  if (kid >= CKernelId::VIEW && kid <= CKernelId::UNFOLD)
    return NodeKind::NOP;

  // Everything else: GEMM, conv, attention, normalization, pooling,
  // embedding, fused, linalg, SSM, inference, 3D, graph, comms, I/O,
  // RNG, OPAQUE → EXTERN (opaque external kernel).
  return NodeKind::EXTERN;
}

// ═══════════════════════════════════════════════════════════════════
// Micro-op instruction set for kernel bodies
// ═══════════════════════════════════════════════════════════════════

enum class MicroOp : uint8_t {
  LOAD, STORE,

  // Arithmetic (mirrors ops_handler.py / BasicMathOpsMixin)
  ADD, SUB, MUL, TRUEDIV, FLOORDIV, MOD,
  NEG, ABS, RECIPROCAL, SQUARE,

  // Comparison
  EQ, NE, LT, LE, GT, GE,

  // Math
  EXP, LOG, LOG2, SQRT, RSQRT,
  SIN, COS, TAN, ASIN, ACOS, ATAN,
  SINH, COSH, TANH, ASINH,
  ERF, CEIL, FLOOR, TRUNC, ROUND,
  SIGMOID, RELU,

  // Bitwise
  BIT_AND, BIT_OR, BIT_XOR, BIT_NOT,
  LSHIFT, RSHIFT,

  // Logic
  AND, OR, NOT,

  // Special
  TO_DTYPE,     // operands[0]=value, target dtype in aux
  CONSTANT,     // Immediate value in aux (int64_t or bitcast double)
  WHERE,        // operands = {cond, true_val, false_val}
  REDUCE,       // operands[0]=value, accumulated by owning node's reduce_op
  INDEX_EXPR,   // Symbolic index (Expr* stored via reinterpret in aux)
};

// ═══════════════════════════════════════════════════════════════════
// Inst: One micro-op instruction (8 bytes, SSA form)
//
// Operands are 0-based indices into the ComputeBody ops array.
// Max 65535 instructions per body (typical kernels: 5-50 ops).
//
//   LOAD:   operands[0] = input buffer index
//   Unary:  operands[0] = source
//   Binary: operands[0] = LHS, [1] = RHS
//   WHERE:  operands[0] = cond, [1] = true, [2] = false
// ═══════════════════════════════════════════════════════════════════

struct Inst {
  MicroOp op{};                    // 1B — zero = LOAD (overwritten before use)
  ScalarType dtype = ScalarType::Undefined; // 1B — result dtype
  uint16_t operands[3]{};          // 6B — SSA references
};

static_assert(sizeof(Inst) == 8, "Inst must be 8 bytes");
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(Inst);

// ═══════════════════════════════════════════════════════════════════
// ComputeBody: Kernel body as micro-op DAG
//
// Replaces inductor's inner_fn closures. A flat array of SSA
// instructions directly emittable to CUDA/HIP C++.
//
// Example: C = relu(A + B)
//   [0] LOAD  buf0, idx     (load from input A)
//   [1] LOAD  buf1, idx     (load from input B)
//   [2] ADD   $0, $1        (element-wise add)
//   [3] RELU  $2            (relu = max(x, 0))
//   [4] STORE $3            (store to output)
// ═══════════════════════════════════════════════════════════════════

struct ComputeBody {
  Inst* ops = nullptr;      // Arena-allocated instruction array
  uint16_t num_ops = 0;     // Total instructions
  uint16_t num_loads = 0;   // LOAD count (= distinct input buffers)
  uint16_t store_op = 0;    // Index of the STORE instruction
  uint16_t pad = 0;
  int64_t* aux = nullptr;   // Per-instruction auxiliary data (arena-allocated).
                             // Non-zero for: CONSTANT (value), TO_DTYPE (target),
                             // INDEX_EXPR (reinterpret_cast<int64_t>(Expr*)).
                             // nullptr until first CONSTANT/TO_DTYPE/INDEX_EXPR.
};

// ═══════════════════════════════════════════════════════════════════
// ExternInfo: Metadata for EXTERN/TEMPLATE nodes
// ═══════════════════════════════════════════════════════════════════

struct ExternInfo {
  const char* python_kernel_name = nullptr; // e.g., "aten.mm.default"
  const char* cpp_kernel_name = nullptr;    // e.g., "at::mm"
  int64_t* constant_args = nullptr;         // Non-tensor constant arguments
  uint16_t num_constant_args = 0;
};

// ═══════════════════════════════════════════════════════════════════
// GraphNode: One computation (64 bytes = one cache line)
//
// Layout manually packed: zero padding waste, verified with
// static_assert. 23K nodes × 64B = 1.4MB (fits in L2 cache).
//
// For REDUCTION nodes, the size array is concatenated:
//   size[0..ndim-1]       = output range expressions
//   size[ndim..ndim+nred-1] = reduction range expressions
// ═══════════════════════════════════════════════════════════════════

struct GraphNode {
  // ── Identity + type (8B) ──────────────────────────
  NodeId id;                    // Unique ID (= buffer name "buf{id}")
  NodeKind kind = NodeKind::NOP; // 1B
  uint8_t flags = 0;            // 1B — NodeFlags bits
  uint8_t ndim = 0;             // 1B — output dimensions
  uint8_t nred = 0;             // 1B — reduction dimensions (0 for non-reductions)

  // ── Layout scalars (8B) ───────────────────────────
  ScalarType dtype = ScalarType::Undefined;     // 1B — output dtype
  ScalarType src_dtype = ScalarType::Undefined; // 1B — source dtype (reductions only)
  int8_t device_idx = -1;       // 1B — (-1 = CPU, 0+ = CUDA device)
  ReduceOp reduce_op{};             // 1B — zero = SUM (only meaningful for REDUCTION kind)
  ReduceHint reduce_hint{};         // 1B — zero = DEFAULT
  uint8_t pad0 = 0;             // 1B
  uint16_t num_inputs = 0;      // 2B

  // ── Pointers (32B) ───────────────────────────────
  const Expr** size = nullptr;    // ndim (+ nred for reductions) symbolic sizes
  const Expr** stride = nullptr;  // nullptr until layout frozen by scheduler
  void* body = nullptr;           // ComputeBody* (pw/red) or ExternInfo* (extern)
  GraphNode** inputs = nullptr;   // Array of num_inputs dependency nodes

  // ── Use tracking (8B) ────────────────────────────
  uint16_t num_uses = 0;        // Live consumer count (for DCE)
  uint16_t num_outputs = 1;     // Output buffers produced (usually 1)
  uint32_t schedule_order = 0;  // Topological order (set by scheduler)

  // ── Scheduler metadata (8B) ──────────────────────
  uint32_t group_hash = 0;      // Hash of (device, ranges) for fusion
  uint32_t fused_group_id = 0;  // Fused group ID (0 = unfused)

  // ── Accessors ──

  bool is_dead() const { return flags & NodeFlags::DEAD; }

  // Reduction range expressions (valid only for REDUCTION kind)
  const Expr** reduction_ranges() const { return size + ndim; }

  ComputeBody* compute_body() const {
    return static_cast<ComputeBody*>(body);
  }

  ExternInfo* extern_info() const {
    return static_cast<ExternInfo*>(body);
  }
};

static_assert(sizeof(GraphNode) == 64, "GraphNode must be 64 bytes");
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(GraphNode);

// ═══════════════════════════════════════════════════════════════════
// Graph: Arena-owned computation graph
//
// All memory is arena-allocated and freed when Graph is destroyed.
// Nodes stored in a flat array indexed by ID: nodes_[id] == node.
//
// Provides:
//   Construction: add_input, add_pointwise, add_reduction, add_extern
//   Transforms:   replace_all_uses (RAUW), eliminate_dead_nodes (DCE),
//                 topological_sort (Kahn's, O(V+E))
// ═══════════════════════════════════════════════════════════════════

class Graph {
 public:
  explicit Graph(ExprPool* pool, SymbolTable* tab = nullptr)
      : pool_(pool), tab_(tab),
        nodes_(nullptr), input_slots_(nullptr), output_slots_(nullptr),
        num_nodes_(0), capacity_(0),
        input_ids_(nullptr), num_inputs_(0),
        output_ids_(nullptr), num_outputs_(0) {
    grow_(1024);
  }

  Graph(const Graph&) = delete("Graph owns an arena; copy would alias or double-free");
  Graph& operator=(const Graph&) = delete("Graph owns an arena; copy would alias or double-free");

  // ── Node construction ──────────────────────────────────────────

  [[nodiscard]] GraphNode* add_input(
      ScalarType dtype, int8_t device_idx,
      std::span<const Expr* const> size) {
    GraphNode* n = alloc_node_();
    n->kind = NodeKind::INPUT;
    n->dtype = dtype;
    n->device_idx = device_idx;
    n->ndim = static_cast<uint8_t>(size.size());
    n->size = copy_exprs_(size);
    return n;
  }

  [[nodiscard]] GraphNode* add_pointwise(
      std::span<const Expr* const> ranges,
      ScalarType dtype, int8_t device_idx,
      ComputeBody* body,
      std::span<GraphNode* const> inputs) {
    GraphNode* n = alloc_node_();
    n->kind = NodeKind::POINTWISE;
    n->dtype = dtype;
    n->device_idx = device_idx;
    n->ndim = static_cast<uint8_t>(ranges.size());
    n->size = copy_exprs_(ranges);
    n->body = body;
    set_inputs_(n, inputs);
    return n;
  }

  [[nodiscard]] GraphNode* add_reduction(
      std::span<const Expr* const> ranges,
      std::span<const Expr* const> red_ranges,
      ReduceOp reduce_op, ReduceHint hint,
      ScalarType dtype, ScalarType src_dtype,
      int8_t device_idx,
      ComputeBody* body,
      std::span<GraphNode* const> inputs) {
    GraphNode* n = alloc_node_();
    n->kind = NodeKind::REDUCTION;
    n->dtype = dtype;
    n->src_dtype = src_dtype;
    n->device_idx = device_idx;
    n->ndim = static_cast<uint8_t>(ranges.size());
    n->nred = static_cast<uint8_t>(red_ranges.size());
    n->reduce_op = reduce_op;
    n->reduce_hint = hint;
    const uint8_t total = n->ndim + n->nred;
    n->size = arena_.alloc_array<const Expr*>(total);
    std::memcpy(
        const_cast<const Expr**>(n->size), ranges.data(),
        ranges.size_bytes());
    std::memcpy(
        const_cast<const Expr**>(n->size) + n->ndim, red_ranges.data(),
        red_ranges.size_bytes());
    n->body = body;
    set_inputs_(n, inputs);
    return n;
  }

  [[nodiscard]] GraphNode* add_extern(
      const char* py_name, const char* cpp_name,
      ScalarType dtype, int8_t device_idx,
      std::span<const Expr* const> size,
      std::span<GraphNode* const> inputs,
      std::span<const int64_t> constant_args = {}) {
    GraphNode* n = alloc_node_();
    n->kind = NodeKind::EXTERN;
    n->dtype = dtype;
    n->device_idx = device_idx;
    n->ndim = static_cast<uint8_t>(size.size());
    n->size = copy_exprs_(size);

    auto* info = arena_.alloc_obj<ExternInfo>();
    info->python_kernel_name = copy_string_(py_name);
    info->cpp_kernel_name = copy_string_(cpp_name);
    info->num_constant_args = static_cast<uint16_t>(constant_args.size());
    if (!constant_args.empty()) {
      info->constant_args = arena_.alloc_array<int64_t>(constant_args.size());
      std::memcpy(info->constant_args, constant_args.data(),
                  constant_args.size_bytes());
    } else {
      info->constant_args = nullptr;
    }
    n->body = info;
    set_inputs_(n, inputs);
    return n;
  }

  // ── ComputeBody helpers ────────────────────────────────────────

  [[nodiscard]] ComputeBody* alloc_body(uint16_t num_ops) {
    auto* b = arena_.alloc_obj<ComputeBody>();
    b->ops = arena_.alloc_array<Inst>(num_ops);
    b->num_ops = num_ops;
    b->num_loads = 0;
    b->store_op = 0;
    b->pad = 0;
    b->aux = nullptr;
    return b;
  }

  // Lazily allocate aux array (only needed for CONSTANT/TO_DTYPE/INDEX_EXPR)
  void alloc_body_aux(ComputeBody* body) {
    if (!body->aux) {
      body->aux = arena_.alloc_array<int64_t>(body->num_ops);
      std::memset(body->aux, 0, body->num_ops * sizeof(int64_t));
    }
  }

  // ── Graph I/O ──────────────────────────────────────────────────

  void set_graph_inputs(std::span<const NodeId> ids) {
    input_ids_ = arena_.alloc_array<NodeId>(ids.size());
    std::memcpy(input_ids_, ids.data(), ids.size_bytes());
    num_inputs_ = static_cast<uint32_t>(ids.size());
  }

  void set_graph_outputs(std::span<const NodeId> ids) {
    output_ids_ = arena_.alloc_array<NodeId>(ids.size());
    std::memcpy(output_ids_, ids.data(), ids.size_bytes());
    num_outputs_ = static_cast<uint32_t>(ids.size());
  }

  // ── Slot ID side-tables ────────────────────────────────────────
  //
  // Parallel to nodes_[], indexed by GraphNode::id. Populated by the
  // lowering pass from TraceEntry; null until set. Kept separate from
  // GraphNode to preserve its 64B cache-line alignment (slot IDs are
  // only accessed during buffer allocation and code emission, not
  // during hot graph traversals like DCE or topological sort).

  void set_input_slots(NodeId node_id, std::span<const SlotId> slots) {
    assert(node_id.raw() < num_nodes_);
    if (slots.empty()) { input_slots_[node_id.raw()] = nullptr; return; }
    input_slots_[node_id.raw()] = arena_.alloc_array<SlotId>(slots.size());
    std::memcpy(input_slots_[node_id.raw()], slots.data(), slots.size_bytes());
  }

  void set_output_slots(NodeId node_id, std::span<const SlotId> slots) {
    assert(node_id.raw() < num_nodes_);
    if (slots.empty()) { output_slots_[node_id.raw()] = nullptr; return; }
    output_slots_[node_id.raw()] = arena_.alloc_array<SlotId>(slots.size());
    std::memcpy(output_slots_[node_id.raw()], slots.data(), slots.size_bytes());
  }

  [[nodiscard]] const SlotId* input_slots(NodeId node_id) const {
    assert(node_id.raw() < num_nodes_);
    return input_slots_[node_id.raw()];
  }

  [[nodiscard]] const SlotId* output_slots(NodeId node_id) const {
    assert(node_id.raw() < num_nodes_);
    return output_slots_[node_id.raw()];
  }

  // ── Transforms ─────────────────────────────────────────────────

  // Replace all uses of old_node with new_node (RAUW).
  // Scans all live nodes, patches inputs arrays, adjusts use counts.
  // O(N × avg_inputs) — ~70μs for a 23K-node graph.
  void replace_all_uses(GraphNode* old_node, GraphNode* new_node) {
    if (old_node == new_node)
      return;
    for (uint32_t i = 0; i < num_nodes_; ++i) {
      GraphNode* n = nodes_[i];
      if (n->flags & NodeFlags::DEAD)
        continue;
      for (uint16_t j = 0; j < n->num_inputs; ++j) {
        if (n->inputs[j] == old_node) {
          n->inputs[j] = new_node;
          --old_node->num_uses;
          ++new_node->num_uses;
        }
      }
    }
    // Patch graph outputs that reference old_node
    for (uint32_t i = 0; i < num_outputs_; ++i) {
      if (output_ids_[i] == old_node->id)
        output_ids_[i] = new_node->id;
    }
  }

  // Dead code elimination. Marks nodes with zero uses and no side
  // effects as DEAD. Propagates: killing a node decrements its
  // inputs' use counts, potentially making them dead too.
  void eliminate_dead_nodes() {
    recompute_uses_();
    bool changed = true;
    while (changed) {
      changed = false;
      for (uint32_t i = num_nodes_; i-- > 0;) {
        GraphNode* n = nodes_[i];
        if (n->flags & NodeFlags::DEAD)
          continue;
        if (n->num_uses == 0 && n->kind != NodeKind::MUTATION) {
          n->flags |= NodeFlags::DEAD;
          for (uint16_t j = 0; j < n->num_inputs; ++j)
            --n->inputs[j]->num_uses;
          changed = true;
        }
      }
    }
  }

  // Topological sort via Kahn's algorithm. Sets schedule_order on
  // each live node. O(V + E) using a flat successor array built
  // from the nodes' inputs lists.
  void topological_sort() {
    auto* in_deg = arena_.alloc_array<uint32_t>(num_nodes_);
    auto* succ_cnt = arena_.alloc_array<uint32_t>(num_nodes_);
    std::memset(in_deg, 0, num_nodes_ * sizeof(uint32_t));
    std::memset(succ_cnt, 0, num_nodes_ * sizeof(uint32_t));

    // Count edges and compute in-degree
    uint32_t total_edges = 0;
    for (uint32_t i = 0; i < num_nodes_; ++i) {
      GraphNode* n = nodes_[i];
      if (n->flags & NodeFlags::DEAD)
        continue;
      for (uint16_t j = 0; j < n->num_inputs; ++j) {
        GraphNode* dep = n->inputs[j];
        if (!(dep->flags & NodeFlags::DEAD)) {
          ++in_deg[i];
          ++succ_cnt[dep->id.raw()];
          ++total_edges;
        }
      }
    }

    // Build flat successor array via prefix-sum offsets
    auto* offset = arena_.alloc_array<uint32_t>(num_nodes_ + 1);
    offset[0] = 0;
    for (uint32_t i = 0; i < num_nodes_; ++i)
      offset[i + 1] = offset[i] + succ_cnt[i];

    auto* succs =
        arena_.alloc_array<uint32_t>(total_edges > 0 ? total_edges : 1);
    std::memset(succ_cnt, 0, num_nodes_ * sizeof(uint32_t));
    for (uint32_t i = 0; i < num_nodes_; ++i) {
      GraphNode* n = nodes_[i];
      if (n->flags & NodeFlags::DEAD)
        continue;
      for (uint16_t j = 0; j < n->num_inputs; ++j) {
        uint32_t dep_id = n->inputs[j]->id.raw();
        if (!(nodes_[dep_id]->flags & NodeFlags::DEAD))
          succs[offset[dep_id] + succ_cnt[dep_id]++] = i;
      }
    }

    // BFS from zero in-degree nodes
    auto* queue = arena_.alloc_array<uint32_t>(num_nodes_);
    uint32_t head = 0, tail = 0;
    for (uint32_t i = 0; i < num_nodes_; ++i) {
      if (!(nodes_[i]->flags & NodeFlags::DEAD) && in_deg[i] == 0)
        queue[tail++] = i;
    }

    uint32_t order = 0;
    while (head < tail) {
      uint32_t id = queue[head++];
      nodes_[id]->schedule_order = order++;
      for (uint32_t k = offset[id]; k < offset[id + 1]; ++k) {
        if (--in_deg[succs[k]] == 0)
          queue[tail++] = succs[k];
      }
    }
  }

  // Clear VISITED flag on all nodes
  void clear_visited() {
    for (uint32_t i = 0; i < num_nodes_; ++i)
      nodes_[i]->flags &= ~NodeFlags::VISITED;
  }

  [[nodiscard]] uint32_t count_live() const {
    uint32_t count = 0;
    for (uint32_t i = 0; i < num_nodes_; ++i) {
      if (!(nodes_[i]->flags & NodeFlags::DEAD))
        ++count;
    }
    return count;
  }

  // ── Accessors ──────────────────────────────────────────────────

  [[nodiscard]] GraphNode* node(NodeId id) const {
    assert(id.raw() < num_nodes_);
    return nodes_[id.raw()];
  }

  [[nodiscard]] GraphNode* node(uint32_t id) const {
    assert(id < num_nodes_);
    return nodes_[id];
  }

  [[nodiscard]] uint32_t num_nodes() const { return num_nodes_; }
  [[nodiscard]] uint32_t num_graph_inputs() const { return num_inputs_; }
  [[nodiscard]] uint32_t num_graph_outputs() const { return num_outputs_; }
  [[nodiscard]] const NodeId* graph_input_ids() const { return input_ids_; }
  [[nodiscard]] const NodeId* graph_output_ids() const { return output_ids_; }

  [[nodiscard]] ExprPool* pool() const { return pool_; }
  [[nodiscard]] SymbolTable* tab() const { return tab_; }
  [[nodiscard]] Arena& arena() { return arena_; }

 private:
  // Allocate a zeroed, 64-byte-aligned GraphNode
  GraphNode* alloc_node_() {
    if (num_nodes_ >= capacity_)
      grow_(capacity_ * 2);
    auto* n = static_cast<GraphNode*>(arena_.alloc(64, 64));
    std::memset(n, 0, 64);
    n->id = NodeId{num_nodes_};
    n->device_idx = -1;
    n->num_outputs = 1;
    nodes_[num_nodes_++] = n;
    return n;
  }

  void grow_(uint32_t new_cap) {
    auto** buf = arena_.alloc_array<GraphNode*>(new_cap);
    auto** is_buf = arena_.alloc_array<SlotId*>(new_cap);
    auto** os_buf = arena_.alloc_array<SlotId*>(new_cap);
    if (nodes_) {
      std::memcpy(buf, nodes_, num_nodes_ * sizeof(GraphNode*));
      std::memcpy(is_buf, input_slots_, num_nodes_ * sizeof(SlotId*));
      std::memcpy(os_buf, output_slots_, num_nodes_ * sizeof(SlotId*));
    }
    // Zero-fill new entries so unset slots read as nullptr.
    std::memset(is_buf + num_nodes_, 0, (new_cap - num_nodes_) * sizeof(SlotId*));
    std::memset(os_buf + num_nodes_, 0, (new_cap - num_nodes_) * sizeof(SlotId*));
    nodes_ = buf;
    input_slots_ = is_buf;
    output_slots_ = os_buf;
    capacity_ = new_cap;
  }

  void set_inputs_(GraphNode* n, std::span<GraphNode* const> inputs) {
    n->num_inputs = static_cast<uint16_t>(inputs.size());
    if (!inputs.empty()) {
      n->inputs = arena_.alloc_array<GraphNode*>(inputs.size());
      std::memcpy(n->inputs, inputs.data(), inputs.size_bytes());
      for (auto* inp : inputs)
        ++inp->num_uses;
    }
  }

  const Expr** copy_exprs_(std::span<const Expr* const> src) {
    if (src.empty())
      return nullptr;
    auto** dst = arena_.alloc_array<const Expr*>(src.size());
    std::memcpy(dst, src.data(), src.size_bytes());
    return dst;
  }

  const char* copy_string_(const char* src) {
    if (!src)
      return nullptr;
    size_t len = std::strlen(src) + 1;
    auto* dst = static_cast<char*>(arena_.alloc(len, 1));
    std::memcpy(dst, src, len);
    return dst;
  }

  // Recompute all use counts from scratch (handles stale counts)
  void recompute_uses_() {
    for (uint32_t i = 0; i < num_nodes_; ++i)
      nodes_[i]->num_uses = 0;

    for (uint32_t i = 0; i < num_nodes_; ++i) {
      GraphNode* n = nodes_[i];
      if (n->flags & NodeFlags::DEAD)
        continue;
      for (uint16_t j = 0; j < n->num_inputs; ++j)
        ++n->inputs[j]->num_uses;
    }
    // Graph outputs are roots: keep them alive
    for (uint32_t i = 0; i < num_outputs_; ++i) {
      if (!(nodes_[output_ids_[i].raw()]->flags & NodeFlags::DEAD))
        ++nodes_[output_ids_[i].raw()]->num_uses;
    }
  }

  Arena arena_;
  ExprPool* pool_;
  SymbolTable* tab_;

  GraphNode** nodes_;
  SlotId** input_slots_;   // [node_id] → per-node input slot ID array (or nullptr)
  SlotId** output_slots_;  // [node_id] → per-node output slot ID array (or nullptr)
  uint32_t num_nodes_;
  uint32_t capacity_;

  NodeId* input_ids_;
  uint32_t num_inputs_;
  NodeId* output_ids_;
  uint32_t num_outputs_;
};

} // namespace crucible
