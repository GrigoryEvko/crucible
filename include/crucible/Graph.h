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
#include <crucible/Expr.h>

#include <cassert>
#include <cstdint>
#include <cstring>

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
  MicroOp op;           // 1B
  int8_t dtype;         // 1B — result dtype (at::ScalarType ordinal)
  uint16_t operands[3]; // 6B — SSA references
};

static_assert(sizeof(Inst) == 8, "Inst must be 8 bytes");

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
  Inst* ops;          // Arena-allocated instruction array
  uint16_t num_ops;   // Total instructions
  uint16_t num_loads; // LOAD count (= distinct input buffers)
  uint16_t store_op;  // Index of the STORE instruction
  uint16_t pad;
  int64_t* aux;       // Per-instruction auxiliary data (arena-allocated).
                      // Non-zero for: CONSTANT (value), TO_DTYPE (target),
                      // INDEX_EXPR (reinterpret_cast<int64_t>(Expr*)).
                      // nullptr until first CONSTANT/TO_DTYPE/INDEX_EXPR.
};

// ═══════════════════════════════════════════════════════════════════
// ExternInfo: Metadata for EXTERN/TEMPLATE nodes
// ═══════════════════════════════════════════════════════════════════

struct ExternInfo {
  const char* python_kernel_name; // e.g., "aten.mm.default"
  const char* cpp_kernel_name;    // e.g., "at::mm"
  int64_t* constant_args;         // Non-tensor constant arguments
  uint16_t num_constant_args;
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
  uint32_t id;          // Unique ID (= buffer name "buf{id}")
  NodeKind kind;        // 1B
  uint8_t flags;        // 1B — NodeFlags bits
  uint8_t ndim;         // 1B — output dimensions
  uint8_t nred;         // 1B — reduction dimensions (0 for non-reductions)

  // ── Layout scalars (8B) ───────────────────────────
  int8_t dtype;         // 1B — output dtype (at::ScalarType ordinal)
  int8_t src_dtype;     // 1B — source dtype (reductions only)
  int8_t device_idx;    // 1B — (-1 = CPU, 0+ = CUDA device)
  ReduceOp reduce_op;   // 1B
  ReduceHint reduce_hint; // 1B
  uint8_t pad0;         // 1B
  uint16_t num_inputs;  // 2B

  // ── Pointers (32B) ───────────────────────────────
  const Expr** size;    // ndim (+ nred for reductions) symbolic sizes
  const Expr** stride;  // nullptr until layout frozen by scheduler
  void* body;           // ComputeBody* (pw/red) or ExternInfo* (extern)
  GraphNode** inputs;   // Array of num_inputs dependency nodes

  // ── Use tracking (8B) ────────────────────────────
  uint16_t num_uses;    // Live consumer count (for DCE)
  uint16_t num_outputs; // Output buffers produced (usually 1)
  uint32_t schedule_order; // Topological order (set by scheduler)

  // ── Scheduler metadata (8B) ──────────────────────
  uint32_t group_hash;     // Hash of (device, ranges) for fusion
  uint32_t fused_group_id; // Fused group ID (0 = unfused)

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
      : pool_(pool), tab_(tab), num_nodes_(0), capacity_(0),
        nodes_(nullptr), input_ids_(nullptr), num_inputs_(0),
        output_ids_(nullptr), num_outputs_(0) {
    grow_(1024);
  }

  Graph(const Graph&) = delete;
  Graph& operator=(const Graph&) = delete;

  // ── Node construction ──────────────────────────────────────────

  GraphNode* add_input(int8_t dtype, int8_t device_idx,
                       const Expr** size, uint8_t ndim) {
    GraphNode* n = alloc_node_();
    n->kind = NodeKind::INPUT;
    n->dtype = dtype;
    n->device_idx = device_idx;
    n->ndim = ndim;
    n->size = copy_exprs_(size, ndim);
    return n;
  }

  GraphNode* add_pointwise(const Expr** ranges, uint8_t ndim,
                           int8_t dtype, int8_t device_idx,
                           ComputeBody* body,
                           GraphNode** inputs, uint16_t ninputs) {
    GraphNode* n = alloc_node_();
    n->kind = NodeKind::POINTWISE;
    n->dtype = dtype;
    n->device_idx = device_idx;
    n->ndim = ndim;
    n->size = copy_exprs_(ranges, ndim);
    n->body = body;
    set_inputs_(n, inputs, ninputs);
    return n;
  }

  GraphNode* add_reduction(const Expr** ranges, uint8_t ndim,
                           const Expr** red_ranges, uint8_t nred,
                           ReduceOp reduce_op, ReduceHint hint,
                           int8_t dtype, int8_t src_dtype,
                           int8_t device_idx,
                           ComputeBody* body,
                           GraphNode** inputs, uint16_t ninputs) {
    GraphNode* n = alloc_node_();
    n->kind = NodeKind::REDUCTION;
    n->dtype = dtype;
    n->src_dtype = src_dtype;
    n->device_idx = device_idx;
    n->ndim = ndim;
    n->nred = nred;
    n->reduce_op = reduce_op;
    n->reduce_hint = hint;
    // Concatenate output + reduction ranges in one array
    uint8_t total = ndim + nred;
    n->size = arena_.alloc_array<const Expr*>(total);
    std::memcpy(
        const_cast<const Expr**>(n->size), ranges,
        ndim * sizeof(const Expr*));
    std::memcpy(
        const_cast<const Expr**>(n->size) + ndim, red_ranges,
        nred * sizeof(const Expr*));
    n->body = body;
    set_inputs_(n, inputs, ninputs);
    return n;
  }

  GraphNode* add_extern(const char* py_name, const char* cpp_name,
                        int8_t dtype, int8_t device_idx,
                        const Expr** size, uint8_t ndim,
                        GraphNode** inputs, uint16_t ninputs,
                        int64_t* constant_args = nullptr,
                        uint16_t nconst = 0) {
    GraphNode* n = alloc_node_();
    n->kind = NodeKind::EXTERN;
    n->dtype = dtype;
    n->device_idx = device_idx;
    n->ndim = ndim;
    n->size = copy_exprs_(size, ndim);

    auto* info = arena_.alloc_obj<ExternInfo>();
    info->python_kernel_name = copy_string_(py_name);
    info->cpp_kernel_name = copy_string_(cpp_name);
    info->num_constant_args = nconst;
    if (nconst > 0) {
      info->constant_args = arena_.alloc_array<int64_t>(nconst);
      std::memcpy(
          info->constant_args, constant_args, nconst * sizeof(int64_t));
    } else {
      info->constant_args = nullptr;
    }
    n->body = info;
    set_inputs_(n, inputs, ninputs);
    return n;
  }

  // ── ComputeBody helpers ────────────────────────────────────────

  ComputeBody* alloc_body(uint16_t num_ops) {
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

  void set_graph_inputs(const uint32_t* ids, uint32_t count) {
    input_ids_ = arena_.alloc_array<uint32_t>(count);
    std::memcpy(input_ids_, ids, count * sizeof(uint32_t));
    num_inputs_ = count;
  }

  void set_graph_outputs(const uint32_t* ids, uint32_t count) {
    output_ids_ = arena_.alloc_array<uint32_t>(count);
    std::memcpy(output_ids_, ids, count * sizeof(uint32_t));
    num_outputs_ = count;
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
          ++succ_cnt[dep->id];
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
        uint32_t dep_id = n->inputs[j]->id;
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

  // Count live (non-DEAD) nodes
  uint32_t count_live() const {
    uint32_t count = 0;
    for (uint32_t i = 0; i < num_nodes_; ++i) {
      if (!(nodes_[i]->flags & NodeFlags::DEAD))
        ++count;
    }
    return count;
  }

  // ── Accessors ──────────────────────────────────────────────────

  GraphNode* node(uint32_t id) const {
    assert(id < num_nodes_);
    return nodes_[id];
  }

  uint32_t num_nodes() const { return num_nodes_; }
  uint32_t num_graph_inputs() const { return num_inputs_; }
  uint32_t num_graph_outputs() const { return num_outputs_; }
  const uint32_t* graph_input_ids() const { return input_ids_; }
  const uint32_t* graph_output_ids() const { return output_ids_; }

  ExprPool* pool() const { return pool_; }
  SymbolTable* tab() const { return tab_; }
  Arena& arena() { return arena_; }

 private:
  // Allocate a zeroed, 64-byte-aligned GraphNode
  GraphNode* alloc_node_() {
    if (num_nodes_ >= capacity_)
      grow_(capacity_ * 2);
    auto* n = static_cast<GraphNode*>(arena_.alloc(64, 64));
    std::memset(n, 0, 64);
    n->id = num_nodes_;
    n->device_idx = -1;
    n->num_outputs = 1;
    nodes_[num_nodes_++] = n;
    return n;
  }

  void grow_(uint32_t new_cap) {
    auto** buf = arena_.alloc_array<GraphNode*>(new_cap);
    if (nodes_)
      std::memcpy(buf, nodes_, num_nodes_ * sizeof(GraphNode*));
    nodes_ = buf;
    capacity_ = new_cap;
  }

  // Wire inputs and increment use counts
  void set_inputs_(GraphNode* n, GraphNode** inputs, uint16_t count) {
    n->num_inputs = count;
    if (count > 0) {
      n->inputs = arena_.alloc_array<GraphNode*>(count);
      std::memcpy(n->inputs, inputs, count * sizeof(GraphNode*));
      for (uint16_t i = 0; i < count; ++i)
        ++inputs[i]->num_uses;
    }
  }

  const Expr** copy_exprs_(const Expr** src, uint8_t count) {
    if (count == 0)
      return nullptr;
    auto** dst = arena_.alloc_array<const Expr*>(count);
    std::memcpy(dst, src, count * sizeof(const Expr*));
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
      if (!(nodes_[output_ids_[i]]->flags & NodeFlags::DEAD))
        ++nodes_[output_ids_[i]]->num_uses;
    }
  }

  Arena arena_;
  ExprPool* pool_;
  SymbolTable* tab_;

  GraphNode** nodes_;
  uint32_t num_nodes_;
  uint32_t capacity_;

  uint32_t* input_ids_;
  uint32_t num_inputs_;
  uint32_t* output_ids_;
  uint32_t num_outputs_;
};

} // namespace crucible
