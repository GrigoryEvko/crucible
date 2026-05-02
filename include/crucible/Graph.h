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
#include <crucible/safety/Bits.h>

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
  NUM_KINDS,  // Sentinel: count of valid kinds. Not a valid NodeKind value.
              // Exhaustive switch asserts against this; also used to size
              // per-kind lookup tables without a magic number.
};

enum class ReduceOp : uint8_t {
  SUM, PROD, MAX, MIN, ARGMAX, ARGMIN, ANY, XOR_SUM, WELFORD, DOT,
  NUM_OPS,    // Sentinel — see NodeKind::NUM_KINDS.
};

// ═══════════════════════════════════════════════════════════════════
// Fusion classification: how two adjacent ops share intermediate data
//
// Modern GPUs (B200: 128 SMs, 256KB regs, 228KB smem, 64KB tmem)
// are "fat": massively parallel but bandwidth-starved. Small ops
// waste >99% of silicon on launch overhead. Fusion fixes this by
// keeping intermediates in faster storage:
//
//   REGISTER:  same thread processes both ops, ~0ns latency
//   SMEM:      same threadblock, shared memory (~20ns latency)
//   EPILOGUE:  GEMM/Conv accumulator → activation without writeback
//   PROLOGUE:  input scaling → GEMM in registers before matmul
//   BROADCAST: reduction output → pointwise via smem broadcast
//
// Each FuseKind implies a storage level for the intermediate.
// The cost model (CostModel.h) uses this to determine effective
// bandwidth: REGISTER = free, SMEM = smem_bw, EPILOGUE = tmem/reg.
// ═══════════════════════════════════════════════════════════════════

enum class FuseKind : uint8_t {
  NONE,        // Cannot fuse — intermediate goes through HBM
  REGISTER,    // Same iteration space: intermediate in registers
  SMEM,        // Same block, different iteration: intermediate via smem
  EPILOGUE,    // EXTERN output stays in accumulator, epilogue applied
  PROLOGUE,    // Input transformed in registers before EXTERN kernel
  BROADCAST,   // Reduction output broadcast to consumers via smem
  NUM_KINDS,   // Sentinel — see NodeKind::NUM_KINDS.
};

enum class ReduceHint : uint8_t { DEFAULT, INNER, OUTER };

// NodeFlags — scoped enum over the 1-byte GraphNode flag bits.
//
// Worn through safety::Bits<NodeFlags> at the field level so the type
// system rejects the dominant bug class: silent mixing of two unrelated
// flag enums on the same uint8_t (e.g. a refactor that writes
// `node.flags |= RecipeFlags::Foo`).  Bits<NodeFlags> and
// Bits<RecipeFlags> are different template instantiations and do NOT
// compose — caught at compile time, not at the next replay divergence.
//
// Layout: underlying uint8_t preserves the 1-byte slot in GraphNode's
// hand-packed 64 B layout (see static_assert(sizeof(GraphNode) == 64)).
enum class NodeFlags : std::uint8_t {
  DEAD     = 1 << 0,
  VISITED  = 1 << 1,
  FUSED    = 1 << 2,
  REALIZED = 1 << 3,
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
  NUM_OPS,      // Sentinel — see NodeKind::NUM_KINDS.
};

// ═══════════════════════════════════════════════════════════════════
// InstIndex: SSA operand reference inside a ComputeBody
//
// A strong 16-bit newtype over the operand index used by Inst.  Its
// job is to prevent silent confusion with the ComputeBody's fellow
// uint16_t scalars (num_ops, num_loads, store_op) — all of which live
// in the same struct and could trivially be swapped into an operand
// slot during a refactor.
//
// InstIndex stays 2 bytes so sizeof(Inst) remains 8.  Aggregate-init
// via `{0, 1, 0}` on an Inst's operands[] continues to work because
// InstIndex is an aggregate with a single uint16_t member.
// ═══════════════════════════════════════════════════════════════════

struct InstIndex {
  uint16_t v = 0;

  constexpr bool operator==(const InstIndex&) const noexcept = default;
  constexpr auto operator<=>(const InstIndex&) const noexcept = default;

  [[nodiscard, gnu::const]] constexpr uint16_t raw() const noexcept { return v; }
};

static_assert(sizeof(InstIndex) == sizeof(uint16_t),
              "InstIndex must stay 2 bytes to preserve sizeof(Inst) == 8");
static_assert(std::is_standard_layout_v<InstIndex>);

// ═══════════════════════════════════════════════════════════════════
// Inst: One micro-op instruction (8 bytes, SSA form)
//
// Operands are 0-based InstIndex references into the ComputeBody ops
// array.  Max 65535 instructions per body (typical kernels: 5-50).
//
//   LOAD:   operands[0] = input buffer index (by SSA convention)
//   Unary:  operands[0] = source
//   Binary: operands[0] = LHS, [1] = RHS
//   WHERE:  operands[0] = cond, [1] = true, [2] = false
// ═══════════════════════════════════════════════════════════════════

struct Inst {
  MicroOp op{};                    // 1B — zero = LOAD (overwritten before use)
  ScalarType dtype = ScalarType::Undefined; // 1B — result dtype
  InstIndex operands[3]{};          // 6B — SSA references (strong-typed)
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
  safety::Bits<NodeFlags> flags{};  // 1B — typed bit-field (sizeof preserved)
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

  [[nodiscard]] bool is_dead() const { return flags.test(NodeFlags::DEAD); }

  // ── Device placement queries ──
  //
  // device_idx uses a sentinel-based encoding: -1 = CPU, 0+ = CUDA device.
  // Raw int8_t access is error-prone (every caller must remember the
  // sentinel, and comparing device_idx < 0 as "CPU" reads like a bug).
  // These accessors make the intent explicit.
  static constexpr int8_t kCpuDeviceIdx = -1;

  [[nodiscard, gnu::pure]] bool is_cpu() const noexcept {
    return device_idx == kCpuDeviceIdx;
  }
  [[nodiscard, gnu::pure]] bool is_gpu() const noexcept {
    return device_idx >= 0;
  }
  // Returns the CUDA device index (0+).  pre-condition enforces the
  // "is_gpu" invariant, so calling on a CPU node fires the contract
  // rather than returning the -1 sentinel as a uint8_t.
  [[nodiscard, gnu::pure]] uint8_t gpu_idx() const noexcept
      pre (is_gpu())
  {
    return static_cast<uint8_t>(device_idx);
  }

  // Reduction range expressions (valid only for REDUCTION kind).
  // pre(nred > 0) ensures the size + ndim offset points into the
  // reduction-range tail rather than past the array end for a
  // non-reducing node.
  [[nodiscard]] const Expr** reduction_ranges() const CRUCIBLE_LIFETIMEBOUND
      pre (kind == NodeKind::REDUCTION)
      pre (nred > 0)
  {
    return size + ndim;
  }

  // body is void* — interpretation depends on `kind`:
  //   POINTWISE / REDUCTION / SCAN → ComputeBody*
  //   EXTERN / TEMPLATE            → ExternInfo*
  //   INPUT / CONSTANT / MUTATION / NOP / SORT → body is null/unused
  //
  // The old accessors compute_body() / extern_info() blindly
  // static_cast<> regardless of kind — a caller with a kind mismatch
  // silently interprets random arena memory as a ComputeBody or
  // ExternInfo struct.  Downstream field reads return garbage.
  //
  // New pre() contracts reject the mismatch: compute_body() fires if
  // kind is not in the ComputeBody set; extern_info() fires if kind
  // is not in the ExternInfo set.  Plus is_* predicates let callers
  // branch before accessing.
  [[nodiscard, gnu::pure]] bool has_compute_body() const noexcept {
    return kind == NodeKind::POINTWISE
        || kind == NodeKind::REDUCTION
        || kind == NodeKind::SCAN;
  }
  [[nodiscard, gnu::pure]] bool has_extern_info() const noexcept {
    return kind == NodeKind::EXTERN || kind == NodeKind::TEMPLATE;
  }

  [[nodiscard]] ComputeBody* compute_body() const CRUCIBLE_LIFETIMEBOUND
      pre (has_compute_body())
      pre (body != nullptr)
  {
    return static_cast<ComputeBody*>(body);
  }

  [[nodiscard]] ExternInfo* extern_info() const CRUCIBLE_LIFETIMEBOUND
      pre (has_extern_info())
      pre (body != nullptr)
  {
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

class CRUCIBLE_OWNER Graph {
 public:
  [[gnu::cold]] explicit Graph(effects::Alloc a, ExprPool* pool, SymbolTable* tab = nullptr)
      : pool_(pool), tab_(tab),
        nodes_(nullptr), input_slots_(nullptr), output_slots_(nullptr),
        // num_nodes_ default-initialized by NSDMI to Monotonic<uint32_t>{0}.
        capacity_(0),
        input_ids_(nullptr), num_inputs_(0),
        output_ids_(nullptr), num_outputs_(0) {
    grow_(a, 1024);
  }

  Graph(const Graph&) = delete("Graph owns an arena; copy would alias or double-free");
  Graph& operator=(const Graph&) = delete("Graph owns an arena; copy would alias or double-free");
  Graph(Graph&&) = delete("interior GraphNode* pointers into arena would dangle");
  Graph& operator=(Graph&&) = delete("interior GraphNode* pointers into arena would dangle");

  // ── Node construction ──────────────────────────────────────────

  [[nodiscard]] GraphNode* add_input(
      effects::Alloc a,
      ScalarType dtype, int8_t device_idx,
      std::span<const Expr* const> size) {
    GraphNode* n = alloc_node_(a);
    n->kind = NodeKind::INPUT;
    n->dtype = dtype;
    n->device_idx = device_idx;
    n->ndim = static_cast<uint8_t>(size.size());
    n->size = copy_exprs_(a, size);
    return n;
  }

  [[nodiscard]] GraphNode* add_pointwise(
      effects::Alloc a,
      std::span<const Expr* const> ranges,
      ScalarType dtype, int8_t device_idx,
      ComputeBody* body,
      std::span<GraphNode* const> inputs) {
    GraphNode* n = alloc_node_(a);
    n->kind = NodeKind::POINTWISE;
    n->dtype = dtype;
    n->device_idx = device_idx;
    n->ndim = static_cast<uint8_t>(ranges.size());
    n->size = copy_exprs_(a, ranges);
    n->body = body;
    set_inputs_(a, n, inputs);
    return n;
  }

  [[nodiscard]] GraphNode* add_reduction(
      effects::Alloc a,
      std::span<const Expr* const> ranges,
      std::span<const Expr* const> red_ranges,
      ReduceOp reduce_op, ReduceHint hint,
      ScalarType dtype, ScalarType src_dtype,
      int8_t device_idx,
      ComputeBody* body,
      std::span<GraphNode* const> inputs) {
    GraphNode* n = alloc_node_(a);
    n->kind = NodeKind::REDUCTION;
    n->dtype = dtype;
    n->src_dtype = src_dtype;
    n->device_idx = device_idx;
    n->ndim = static_cast<uint8_t>(ranges.size());
    n->nred = static_cast<uint8_t>(red_ranges.size());
    n->reduce_op = reduce_op;
    n->reduce_hint = hint;
    const auto total = static_cast<uint8_t>(n->ndim + n->nred);
    n->size = arena_.alloc_array<const Expr*>(a, total);
    // memcpy(nullptr, ..., 0) is UB; guard by size.  alloc_array(0)
    // yields nullptr and ranges.data() is nullptr for an empty span.
    if (!ranges.empty()) {
      std::memcpy(const_cast<const Expr**>(n->size), ranges.data(),
                  ranges.size_bytes());
    }
    if (!red_ranges.empty()) {
      std::memcpy(const_cast<const Expr**>(n->size) + n->ndim,
                  red_ranges.data(), red_ranges.size_bytes());
    }
    n->body = body;
    set_inputs_(a, n, inputs);
    return n;
  }

  [[nodiscard]] GraphNode* add_extern(
      effects::Alloc a,
      const char* py_name, const char* cpp_name,
      ScalarType dtype, int8_t device_idx,
      std::span<const Expr* const> size,
      std::span<GraphNode* const> inputs,
      std::span<const int64_t> constant_args = {}) {
    GraphNode* n = alloc_node_(a);
    n->kind = NodeKind::EXTERN;
    n->dtype = dtype;
    n->device_idx = device_idx;
    n->ndim = static_cast<uint8_t>(size.size());
    n->size = copy_exprs_(a, size);

    auto* info = arena_.alloc_obj<ExternInfo>(a);
    info->python_kernel_name = copy_string_(a, py_name);
    info->cpp_kernel_name = copy_string_(a, cpp_name);
    info->num_constant_args = static_cast<uint16_t>(constant_args.size());
    if (!constant_args.empty()) {
      info->constant_args = arena_.alloc_array<int64_t>(a, constant_args.size());
      std::memcpy(info->constant_args, constant_args.data(),
                  constant_args.size_bytes());
    } else {
      info->constant_args = nullptr;
    }
    n->body = info;
    set_inputs_(a, n, inputs);
    return n;
  }

  // ── ComputeBody helpers ────────────────────────────────────────

  [[nodiscard]] ComputeBody* alloc_body(effects::Alloc a, uint16_t num_ops) {
    auto* b = arena_.alloc_obj<ComputeBody>(a);
    b->ops = arena_.alloc_array<Inst>(a, num_ops);
    b->num_ops = num_ops;
    b->num_loads = 0;
    b->store_op = 0;
    b->pad = 0;
    b->aux = nullptr;
    return b;
  }

  // Lazily allocate aux array (only needed for CONSTANT/TO_DTYPE/INDEX_EXPR)
  void alloc_body_aux(effects::Alloc a, ComputeBody* body) {
    if (!body->aux) {
      body->aux = arena_.alloc_array<int64_t>(a, body->num_ops);
      std::memset(body->aux, 0, body->num_ops * sizeof(int64_t));
    }
  }

  // ── Graph I/O ──────────────────────────────────────────────────

  void set_graph_inputs(effects::Alloc a, std::span<const NodeId> ids) {
    num_inputs_ = static_cast<uint32_t>(ids.size());
    if (ids.empty()) { input_ids_ = nullptr; return; }
    input_ids_ = arena_.alloc_array<NodeId>(a, ids.size());
    std::memcpy(input_ids_, ids.data(), ids.size_bytes());
  }

  void set_graph_outputs(effects::Alloc a, std::span<const NodeId> ids) {
    num_outputs_ = static_cast<uint32_t>(ids.size());
    if (ids.empty()) { output_ids_ = nullptr; return; }
    output_ids_ = arena_.alloc_array<NodeId>(a, ids.size());
    std::memcpy(output_ids_, ids.data(), ids.size_bytes());
  }

  // ── Slot ID side-tables ────────────────────────────────────────
  //
  // Parallel to nodes_[], indexed by GraphNode::id. Populated by the
  // lowering pass from TraceEntry; null until set. Kept separate from
  // GraphNode to preserve its 64B cache-line alignment (slot IDs are
  // only accessed during buffer allocation and code emission, not
  // during hot graph traversals like DCE or topological sort).

  void set_input_slots(effects::Alloc a, NodeId node_id, std::span<const SlotId> slots)
      pre (node_id.raw() < num_nodes_.get())
  {
    if (slots.empty()) { input_slots_[node_id.raw()] = nullptr; return; }
    input_slots_[node_id.raw()] = arena_.alloc_array<SlotId>(a, slots.size());
    std::memcpy(input_slots_[node_id.raw()], slots.data(), slots.size_bytes());
  }

  void set_output_slots(effects::Alloc a, NodeId node_id, std::span<const SlotId> slots)
      pre (node_id.raw() < num_nodes_.get())
  {
    if (slots.empty()) { output_slots_[node_id.raw()] = nullptr; return; }
    output_slots_[node_id.raw()] = arena_.alloc_array<SlotId>(a, slots.size());
    std::memcpy(output_slots_[node_id.raw()], slots.data(), slots.size_bytes());
  }

  [[nodiscard]] const SlotId* input_slots(NodeId node_id) const CRUCIBLE_LIFETIMEBOUND
      pre (node_id.raw() < num_nodes_.get())
  {
    return input_slots_[node_id.raw()];
  }

  [[nodiscard]] const SlotId* output_slots(NodeId node_id) const CRUCIBLE_LIFETIMEBOUND
      pre (node_id.raw() < num_nodes_.get())
  {
    return output_slots_[node_id.raw()];
  }

  // ── Transforms ─────────────────────────────────────────────────

  // Replace all uses of old_node with new_node (RAUW).
  // Scans all live nodes, patches inputs arrays, adjusts use counts.
  // O(N × avg_inputs) — ~70μs for a 23K-node graph.
  void replace_all_uses(GraphNode* old_node, GraphNode* new_node) {
    if (old_node == new_node)
      return;
    const uint32_t n_nodes = num_nodes_.get();
    for (uint32_t i = 0; i < n_nodes; ++i) {
      GraphNode* n = nodes_[i];
      if (n->flags.test(NodeFlags::DEAD))
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
  //
  // Post: every live (non-DEAD) non-MUTATION node has num_uses > 0
  // OR is referenced as a graph output.  Caller relies on this for
  // correctness of downstream passes (topological_sort skips DEAD).
  void eliminate_dead_nodes() noexcept {
    recompute_uses_();
    bool changed = true;
    while (changed) {
      changed = false;
      for (uint32_t i = num_nodes_.get(); i-- > 0;) {
        GraphNode* current_node = nodes_[i];
        if (current_node->flags.test(NodeFlags::DEAD))
          continue;
        if (current_node->num_uses == 0 && current_node->kind != NodeKind::MUTATION) {
          current_node->flags.set(NodeFlags::DEAD);
          for (uint16_t j = 0; j < current_node->num_inputs; ++j)
            --current_node->inputs[j]->num_uses;
          changed = true;
        }
      }
    }
  }

  // Topological sort via Kahn's algorithm. Sets schedule_order on
  // each live node. O(V + E) using a flat successor array built
  // from the nodes' inputs lists.
  void topological_sort(effects::Alloc a) {
    const uint32_t n_nodes = num_nodes_.get();
    auto* in_deg = arena_.alloc_array<uint32_t>(a, n_nodes);
    auto* succ_cnt = arena_.alloc_array<uint32_t>(a, n_nodes);
    std::memset(in_deg, 0, n_nodes * sizeof(uint32_t));
    std::memset(succ_cnt, 0, n_nodes * sizeof(uint32_t));

    // Count edges and compute in-degree
    uint32_t total_edges = 0;
    for (uint32_t i = 0; i < n_nodes; ++i) {
      GraphNode* current_node = nodes_[i];
      if (current_node->flags.test(NodeFlags::DEAD))
        continue;
      for (uint16_t j = 0; j < current_node->num_inputs; ++j) {
        GraphNode* dep_node = current_node->inputs[j];
        if (!(dep_node->flags.test(NodeFlags::DEAD))) {
          ++in_deg[i];
          ++succ_cnt[dep_node->id.raw()];
          ++total_edges;
        }
      }
    }

    // Build flat successor array via prefix-sum offsets
    auto* offset = arena_.alloc_array<uint32_t>(a, n_nodes + 1);
    offset[0] = 0;
    for (uint32_t i = 0; i < n_nodes; ++i)
      offset[i + 1] = offset[i] + succ_cnt[i];

    auto* succs =
        arena_.alloc_array<uint32_t>(a, total_edges > 0 ? total_edges : 1);
    std::memset(succ_cnt, 0, n_nodes * sizeof(uint32_t));
    for (uint32_t i = 0; i < n_nodes; ++i) {
      GraphNode* current_node = nodes_[i];
      if (current_node->flags.test(NodeFlags::DEAD))
        continue;
      for (uint16_t j = 0; j < current_node->num_inputs; ++j) {
        uint32_t dep_id = current_node->inputs[j]->id.raw();
        if (!(nodes_[dep_id]->flags.test(NodeFlags::DEAD)))
          succs[offset[dep_id] + succ_cnt[dep_id]++] = i;
      }
    }

    // BFS from zero in-degree nodes
    auto* queue = arena_.alloc_array<uint32_t>(a, n_nodes);
    uint32_t head = 0, tail = 0;
    for (uint32_t i = 0; i < n_nodes; ++i) {
      if (!(nodes_[i]->flags.test(NodeFlags::DEAD)) && in_deg[i] == 0)
        queue[tail++] = i;
    }

    uint32_t next_schedule_order = 0;
    while (head < tail) {
      uint32_t dequeued_node_id = queue[head++];
      nodes_[dequeued_node_id]->schedule_order = next_schedule_order++;
      for (uint32_t k = offset[dequeued_node_id]; k < offset[dequeued_node_id + 1]; ++k) {
        if (--in_deg[succs[k]] == 0)
          queue[tail++] = succs[k];
      }
    }
  }

  // Common Subexpression Elimination. Finds structurally identical
  // nodes (same kind, dtype, sizes, inputs, body) and replaces
  // duplicates with the first occurrence. Processes in topological
  // order so all inputs are canonicalized before dependents.
  // Returns the number of eliminated nodes.
  //
  // Complexity: O(V + E) — single topo sort, single hash pass,
  // single rewrite pass. No per-elimination RAUW scan.
  //
  // Uses a canonical_representative[] map: during the hash pass, inputs
  // are looked up through the map (not physically rewritten). After the
  // pass, one rewrite sweeps all live nodes to patch input pointers.
  [[nodiscard]] uint32_t eliminate_common_subexpressions(effects::Alloc a) {
    topological_sort(a);

    const uint32_t n_nodes = num_nodes_.get();

    // Canonical map: node_id → canonical representative.
    // Initially identity. Updated when a duplicate is found.
    auto* canonical_representative = arena_.alloc_array<GraphNode*>(a, n_nodes);
    for (uint32_t i = 0; i < n_nodes; ++i)
      canonical_representative[i] = nodes_[i];

    // Build processing order: O(n) scatter via schedule_order
    // (schedule_order is 0..num_live_nodes-1 from topological_sort)
    uint32_t num_live_nodes = 0;
    for (uint32_t i = 0; i < n_nodes; ++i)
      if (!(nodes_[i]->flags.test(NodeFlags::DEAD))) ++num_live_nodes;
    auto* topological_order = arena_.alloc_array<GraphNode*>(a, num_live_nodes > 0 ? num_live_nodes : 1);
    for (uint32_t i = 0; i < n_nodes; ++i) {
      if (!(nodes_[i]->flags.test(NodeFlags::DEAD)))
        topological_order[nodes_[i]->schedule_order] = nodes_[i];
    }

    // Open-addressing hash table: ~50% load factor
    uint32_t cse_table_capacity = std::bit_ceil(num_live_nodes * 2 + 1);
    auto* cse_table_hashes = arena_.alloc_array<uint64_t>(a, cse_table_capacity);
    auto* cse_table_nodes = arena_.alloc_array<GraphNode*>(a, cse_table_capacity);
    std::memset(cse_table_nodes, 0, cse_table_capacity * sizeof(GraphNode*));

    uint32_t eliminated_count = 0;
    uint32_t cse_table_index_mask = cse_table_capacity - 1;
    for (uint32_t i = 0; i < num_live_nodes; ++i) {
      GraphNode* current_node = topological_order[i];
      if (current_node->kind == NodeKind::INPUT || current_node->kind == NodeKind::MUTATION)
        continue;

      uint64_t node_cse_hash = cse_hash_(current_node, canonical_representative);

      for (uint32_t probe_iteration = 0; probe_iteration < cse_table_capacity; ++probe_iteration) {
        uint32_t probe_slot_index = (static_cast<uint32_t>(node_cse_hash) + probe_iteration) & cse_table_index_mask;
        if (!cse_table_nodes[probe_slot_index]) {
          cse_table_hashes[probe_slot_index] = node_cse_hash;
          cse_table_nodes[probe_slot_index] = current_node;
          break;
        }
        if (cse_table_hashes[probe_slot_index] == node_cse_hash &&
            cse_equal_(current_node, cse_table_nodes[probe_slot_index], canonical_representative)) {
          canonical_representative[current_node->id.raw()] = cse_table_nodes[probe_slot_index];
          current_node->flags.set(NodeFlags::DEAD);
          ++eliminated_count;
          break;
        }
      }
    }

    if (eliminated_count > 0) {
      // Single O(V × avg_inputs) rewrite pass
      for (uint32_t i = 0; i < n_nodes; ++i) {
        GraphNode* current_node = nodes_[i];
        if (current_node->flags.test(NodeFlags::DEAD)) continue;
        for (uint16_t j = 0; j < current_node->num_inputs; ++j)
          current_node->inputs[j] = canonical_representative[current_node->inputs[j]->id.raw()];
      }
      // Patch graph outputs
      for (uint32_t i = 0; i < num_outputs_; ++i)
        output_ids_[i] = canonical_representative[output_ids_[i].raw()]->id;
      // Recompute use counts after bulk rewrite
      recompute_uses_();
    }
    return eliminated_count;
  }

  // ── Fusion Group Computation ──────────────────────────────────────
  //
  // Assigns fused_group_id to nodes that can execute in one kernel.
  // Fusion eliminates intermediate HBM traffic: producer's output
  // stays in registers/shared memory instead of round-tripping to DRAM.
  //
  // Fusible pairs:
  //   POINTWISE → POINTWISE  (same ranges → one kernel loop)
  //   POINTWISE → REDUCTION  (produce + reduce in one launch)
  //
  // Not fusible: EXTERN (opaque), INPUT, CONSTANT, NOP, MUTATION,
  //              SCAN, SORT, TEMPLATE (own launch logic)
  //
  // Algorithm: greedy propagation in topological order.
  // For each fusible node, join the group of its first compatible
  // input. If no compatible input exists, create a new group.
  // Compatible = same device + same output ranges (Expr* equality).
  //
  // Also computes group_hash on each node for quick compatibility
  // checking by downstream passes.
  //
  // Returns the number of fusion groups created.
  [[nodiscard]] uint32_t compute_fusion_groups(effects::Alloc a) {
    topological_sort(a);

    const uint32_t n_nodes = num_nodes_.get();

    // Build ordered list via O(n) scatter
    uint32_t num_live_nodes = 0;
    for (uint32_t i = 0; i < n_nodes; ++i)
      if (!(nodes_[i]->flags.test(NodeFlags::DEAD))) ++num_live_nodes;
    auto* topological_order = arena_.alloc_array<GraphNode*>(a, num_live_nodes > 0 ? num_live_nodes : 1);
    for (uint32_t i = 0; i < n_nodes; ++i) {
      if (!(nodes_[i]->flags.test(NodeFlags::DEAD)))
        topological_order[nodes_[i]->schedule_order] = nodes_[i];
    }

    // Reset all group IDs
    for (uint32_t i = 0; i < n_nodes; ++i) {
      nodes_[i]->fused_group_id = 0;
      nodes_[i]->group_hash = 0;
    }

    // Compute group_hash for each live node: hash of (device, ranges).
    // Nodes with different group_hash can never fuse.
    //
    // Same REFL-4 rationale as cse_hash_ above: this is SELECTIVE on
    // GraphNode fields (only device_idx + ndim + size, which determine
    // fusion-group eligibility — kind/dtype/etc. are intentionally
    // excluded so different-kind nodes can still group).  Pure
    // reflect_hash<GraphNode> would over-fold and break grouping.
    // Manual selection is load-bearing.
    for (uint32_t i = 0; i < num_live_nodes; ++i) {
      GraphNode* current_node = topological_order[i];
      uint64_t node_group_hash = detail::fmix64(
          static_cast<uint64_t>(static_cast<uint8_t>(current_node->device_idx)) |
          (static_cast<uint64_t>(current_node->ndim) << 8));
      for (uint8_t d = 0; d < current_node->ndim; ++d)
        node_group_hash = detail::wymix(node_group_hash, reinterpret_cast<uint64_t>(current_node->size[d]));
      current_node->group_hash = static_cast<uint32_t>(node_group_hash);
    }

    uint32_t next_fusion_group_id = 1;
    for (uint32_t i = 0; i < num_live_nodes; ++i) {
      GraphNode* current_node = topological_order[i];
      if (!is_fusible_(current_node->kind))
        continue;

      // Try to join an input's group
      for (uint16_t j = 0; j < current_node->num_inputs; ++j) {
        GraphNode* input_node = current_node->inputs[j];
        if (input_node->fused_group_id == 0) continue;
        if (!is_fusible_(input_node->kind)) continue;
        if (input_node->group_hash != current_node->group_hash) continue;
        // Full ranges check (group_hash collision possible)
        if (ranges_compatible_(current_node, input_node)) {
          current_node->fused_group_id = input_node->fused_group_id;
          current_node->flags.set(NodeFlags::FUSED);
          break;
        }
      }
      if (current_node->fused_group_id == 0)
        current_node->fused_group_id = next_fusion_group_id++;
    }
    return next_fusion_group_id - 1;
  }

  // Count nodes in a specific fusion group
  [[nodiscard, gnu::pure]] uint32_t group_size(uint32_t group_id) const noexcept {
    uint32_t match_count = 0;
    const uint32_t n_nodes = num_nodes_.get();
    for (uint32_t i = 0; i < n_nodes; ++i)
      if (nodes_[i]->fused_group_id == group_id) ++match_count;
    return match_count;
  }

  // Clear VISITED flag on all nodes — Bits<E>::unset turns the manual
  // ~bitmask + AND-NOT pattern into a single typed call site.
  void clear_visited() {
    const uint32_t n_nodes = num_nodes_.get();
    for (uint32_t i = 0; i < n_nodes; ++i)
      nodes_[i]->flags.unset(NodeFlags::VISITED);
  }

  [[nodiscard, gnu::pure]] uint32_t count_live() const noexcept {
    uint32_t live_count = 0;
    const uint32_t n_nodes = num_nodes_.get();
    for (uint32_t i = 0; i < n_nodes; ++i) {
      if (!(nodes_[i]->flags.test(NodeFlags::DEAD)))
        ++live_count;
    }
    return live_count;
  }

  // ── Accessors ──────────────────────────────────────────────────

  [[nodiscard, gnu::pure]] GraphNode* node(NodeId id) const noexcept CRUCIBLE_LIFETIMEBOUND
      pre (id.raw() < num_nodes_.get())
  {
    [[assume(id.raw() < num_nodes_.get())]];
    return nodes_[id.raw()];
  }

  [[nodiscard, gnu::pure]] GraphNode* node(uint32_t id) const noexcept CRUCIBLE_LIFETIMEBOUND
      pre (id < num_nodes_.get())
  {
    [[assume(id < num_nodes_.get())]];
    return nodes_[id];
  }

  [[nodiscard, gnu::pure]] uint32_t num_nodes() const noexcept { return num_nodes_.get(); }
  [[nodiscard, gnu::pure]] uint32_t num_graph_inputs() const noexcept { return num_inputs_; }
  [[nodiscard, gnu::pure]] uint32_t num_graph_outputs() const noexcept { return num_outputs_; }
  [[nodiscard, gnu::pure]] const NodeId* graph_input_ids() const noexcept CRUCIBLE_LIFETIMEBOUND { return input_ids_; }
  [[nodiscard, gnu::pure]] const NodeId* graph_output_ids() const noexcept CRUCIBLE_LIFETIMEBOUND { return output_ids_; }

  [[nodiscard, gnu::pure]] ExprPool* pool() const noexcept CRUCIBLE_LIFETIMEBOUND { return pool_; }
  [[nodiscard, gnu::pure]] SymbolTable* tab() const noexcept CRUCIBLE_LIFETIMEBOUND { return tab_; }
  [[nodiscard]] Arena& arena() noexcept CRUCIBLE_LIFETIMEBOUND { return arena_; }

 private:
  // Allocate a zeroed, 64-byte-aligned GraphNode
  GraphNode* alloc_node_(effects::Alloc a) {
    if (num_nodes_.get() >= capacity_)
      grow_(a, capacity_ * 2);
    auto* n = ::new (arena_.alloc_obj<GraphNode>(a)) GraphNode{};
    n->id = NodeId{num_nodes_.get()};
    nodes_[num_nodes_.get()] = n;
    num_nodes_.bump();   // contract catches wraparound at uint32_t::max
    return n;
  }

  void grow_(effects::Alloc a, uint32_t new_cap) {
    auto** buf = arena_.alloc_array<GraphNode*>(a, new_cap);
    auto** is_buf = arena_.alloc_array<SlotId*>(a, new_cap);
    auto** os_buf = arena_.alloc_array<SlotId*>(a, new_cap);
    const uint32_t n_nodes = num_nodes_.get();
    if (nodes_) {
      std::memcpy(buf, nodes_, n_nodes * sizeof(GraphNode*));
      std::memcpy(is_buf, input_slots_, n_nodes * sizeof(SlotId*));
      std::memcpy(os_buf, output_slots_, n_nodes * sizeof(SlotId*));
    }
    // Zero-fill new entries so unset slots read as nullptr.
    std::memset(is_buf + n_nodes, 0, (new_cap - n_nodes) * sizeof(SlotId*));
    std::memset(os_buf + n_nodes, 0, (new_cap - n_nodes) * sizeof(SlotId*));
    nodes_ = buf;
    input_slots_ = is_buf;
    output_slots_ = os_buf;
    capacity_ = new_cap;
  }

  void set_inputs_(effects::Alloc a, GraphNode* n, std::span<GraphNode* const> inputs) {
    n->num_inputs = static_cast<uint16_t>(inputs.size());
    if (!inputs.empty()) {
      n->inputs = arena_.alloc_array<GraphNode*>(a, inputs.size());
      std::memcpy(n->inputs, inputs.data(), inputs.size_bytes());
      for (auto* inp : inputs)
        ++inp->num_uses;
    }
  }

  const Expr** copy_exprs_(effects::Alloc a, std::span<const Expr* const> src) {
    if (src.empty())
      return nullptr;
    auto** dst = arena_.alloc_array<const Expr*>(a, src.size());
    std::memcpy(dst, src.data(), src.size_bytes());
    return dst;
  }

  const char* copy_string_(effects::Alloc a, const char* src) {
    if (!src)
      return nullptr;
    size_t len = std::strlen(src) + 1;
    auto* dst = static_cast<char*>(arena_.alloc(a,
        crucible::safety::Positive<size_t>{len},
        crucible::safety::PowerOfTwo<size_t>{1}));
    std::memcpy(dst, src, len);
    return dst;
  }

  // ── Fusion helpers ──────────────────────────────────────────────

  // Exhaustive classification.  A new NodeKind added without updating
  // this switch fires -Werror=switch; the default: arm would silently
  // return false under the old if-chain, hiding the bug.
  [[nodiscard, gnu::const]] static bool is_fusible_(NodeKind kind) noexcept {
    switch (kind) {
      case NodeKind::POINTWISE:
      case NodeKind::REDUCTION:
        return true;
      case NodeKind::INPUT:
      case NodeKind::CONSTANT:
      case NodeKind::SCAN:
      case NodeKind::SORT:
      case NodeKind::EXTERN:
      case NodeKind::TEMPLATE:
      case NodeKind::MUTATION:
      case NodeKind::NOP:
        return false;
      default:
        std::unreachable();
    }
  }

  // Two nodes have compatible ranges if same device + same output
  // dimensions. Expr* is interned → pointer equality per dimension.
  [[nodiscard]] static bool ranges_compatible_(const GraphNode* lhs_node, const GraphNode* rhs_node) {
    if (lhs_node->device_idx != rhs_node->device_idx) return false;
    if (lhs_node->ndim != rhs_node->ndim) return false;
    for (uint8_t d = 0; d < lhs_node->ndim; ++d)
      if (lhs_node->size[d] != rhs_node->size[d]) return false;
    return true;
  }

  // ── CSE helpers ──────────────────────────────────────────────────

  // Structural hash for CSE. Uses canonical[] to resolve inputs
  // without physically rewriting pointers during the pass.
  //
  // ─── Family-B (process-local) per Types.h taxonomy ─────────────
  // Mixes `reinterpret_cast<uintptr_t>(canonical[...])` and
  // `reinterpret_cast<uintptr_t>(n->size[d])` (interned Expr*) as
  // entropy sources — arena pointers are ASLR-randomized per process,
  // so the output is NOT cross-process stable.  This is fine for its
  // single purpose: CSE probing within one compile pass on the bg
  // thread.  MUST NOT be persisted or fed into any Cipher key.
  //
  // ─── Why NOT reflect_hash<GraphNode>? (REFL-4) ─────────────────────
  //
  // Pure reflect_hash<GraphNode> would iterate ALL non-static data
  // members — including fields that are NOT part of CSE identity:
  //   - `id` — unique per node, defeats the entire point of CSE
  //   - output edges, parent links — derivative of structural shape
  //   - scratch fields written by later passes
  //
  // Including those would change CSE semantics, not just bit pattern:
  // two structurally-equivalent nodes would receive different hashes
  // and miss collapse, producing incorrect (but valid-looking)
  // graphs.  The manual hash here projects exactly the structural
  // fields and uses kind-conditional logic for the body / extern /
  // reduce-op portions.
  //
  // When C++26 annotations (P3394R4) land in the ecosystem, this
  // becomes a candidate for `[[crucible::cse_hash::include]]`-style
  // field tagging + annotation-aware reflection.  Until then, the
  // explicit selection here is load-bearing and must stay manual.
  [[nodiscard]] static uint64_t cse_hash_(
      const GraphNode* node, const GraphNode* const* canonical) {
    uint64_t structural_hash = detail::fmix64(
        static_cast<uint64_t>(std::to_underlying(node->kind)) |
        (static_cast<uint64_t>(std::to_underlying(node->dtype)) << 8) |
        (static_cast<uint64_t>(static_cast<uint8_t>(node->device_idx)) << 16) |
        (static_cast<uint64_t>(node->ndim) << 24) |
        (static_cast<uint64_t>(node->nred) << 32));

    // Size expressions (interned → pointer identity)
    const auto total_dims = static_cast<uint8_t>(node->ndim + node->nred);
    for (uint8_t d = 0; d < total_dims; ++d)
      structural_hash = detail::wymix(structural_hash, reinterpret_cast<uint64_t>(node->size[d]));

    // Inputs via canonical map (not raw pointers)
    for (uint16_t j = 0; j < node->num_inputs; ++j)
      structural_hash = detail::wymix(structural_hash,
          reinterpret_cast<uint64_t>(canonical[node->inputs[j]->id.raw()]));

    // Body ops (POINTWISE/REDUCTION): pack each Inst into 8 bytes
    if ((node->kind == NodeKind::POINTWISE || node->kind == NodeKind::REDUCTION) && node->body) {
      auto* body = node->compute_body();
      // Inst is 8 bytes, trivially copyable → hash as uint64_t
      static_assert(sizeof(Inst) == 8);
      for (uint16_t k = 0; k < body->num_ops; ++k) {
        uint64_t packed_inst;
        std::memcpy(&packed_inst, &body->ops[k], 8);
        structural_hash = detail::wymix(structural_hash, packed_inst);
      }
    }

    // Extern kernel name
    if (node->kind == NodeKind::EXTERN && node->body) {
      auto* info = node->extern_info();
      if (info->python_kernel_name)
        for (const char* char_cursor = info->python_kernel_name; *char_cursor; ++char_cursor)
          structural_hash = detail::wymix(structural_hash, static_cast<uint64_t>(*char_cursor));
    }

    // Reduce op for reductions
    if (node->kind == NodeKind::REDUCTION)
      structural_hash ^= detail::fmix64(static_cast<uint64_t>(std::to_underlying(node->reduce_op)));

    return structural_hash;
  }

  // Structural equality for CSE. Looks through canonical[] for inputs.
  [[nodiscard]] static bool cse_equal_(
      const GraphNode* lhs_node, const GraphNode* rhs_node,
      const GraphNode* const* canonical) {
    if (lhs_node->kind != rhs_node->kind || lhs_node->dtype != rhs_node->dtype ||
        lhs_node->device_idx != rhs_node->device_idx || lhs_node->ndim != rhs_node->ndim ||
        lhs_node->nred != rhs_node->nred || lhs_node->num_inputs != rhs_node->num_inputs)
      return false;

    // Sizes (interned → pointer equality)
    const auto total_dims = static_cast<uint8_t>(lhs_node->ndim + lhs_node->nred);
    for (uint8_t d = 0; d < total_dims; ++d)
      if (lhs_node->size[d] != rhs_node->size[d]) return false;

    // Inputs via canonical map
    for (uint16_t j = 0; j < lhs_node->num_inputs; ++j)
      if (canonical[lhs_node->inputs[j]->id.raw()] != canonical[rhs_node->inputs[j]->id.raw()])
        return false;

    // Body equality (POINTWISE/REDUCTION)
    if (lhs_node->kind == NodeKind::POINTWISE || lhs_node->kind == NodeKind::REDUCTION) {
      auto* lhs_body = lhs_node->compute_body();
      auto* rhs_body = rhs_node->compute_body();
      if (lhs_body != rhs_body) {
        if (!lhs_body || !rhs_body || lhs_body->num_ops != rhs_body->num_ops) return false;
        if (std::memcmp(lhs_body->ops, rhs_body->ops, lhs_body->num_ops * sizeof(Inst)) != 0) return false;
        if (lhs_body->aux != rhs_body->aux) {
          if (!lhs_body->aux || !rhs_body->aux) return false;
          if (std::memcmp(lhs_body->aux, rhs_body->aux, lhs_body->num_ops * sizeof(int64_t)) != 0) return false;
        }
      }
    }

    // Reduction-specific fields
    if (lhs_node->kind == NodeKind::REDUCTION) {
      if (lhs_node->reduce_op != rhs_node->reduce_op ||
          lhs_node->reduce_hint != rhs_node->reduce_hint ||
          lhs_node->src_dtype != rhs_node->src_dtype)
        return false;
    }

    // Extern-specific fields
    if (lhs_node->kind == NodeKind::EXTERN) {
      auto* lhs_info = lhs_node->extern_info();
      auto* rhs_info = rhs_node->extern_info();
      if (lhs_info != rhs_info) {
        if (!lhs_info || !rhs_info) return false;
        if (lhs_info->python_kernel_name != rhs_info->python_kernel_name) {
          if (!lhs_info->python_kernel_name || !rhs_info->python_kernel_name) return false;
          if (std::strcmp(lhs_info->python_kernel_name, rhs_info->python_kernel_name) != 0) return false;
        }
        if (lhs_info->num_constant_args != rhs_info->num_constant_args) return false;
        if (lhs_info->num_constant_args > 0 &&
            std::memcmp(lhs_info->constant_args, rhs_info->constant_args,
                        lhs_info->num_constant_args * sizeof(int64_t)) != 0)
          return false;
      }
    }

    return true;
  }

  // Recompute all use counts from scratch (handles stale counts)
  void recompute_uses_() {
    const uint32_t n_nodes = num_nodes_.get();
    for (uint32_t i = 0; i < n_nodes; ++i)
      nodes_[i]->num_uses = 0;

    for (uint32_t i = 0; i < n_nodes; ++i) {
      GraphNode* current_node = nodes_[i];
      if (current_node->flags.test(NodeFlags::DEAD))
        continue;
      for (uint16_t j = 0; j < current_node->num_inputs; ++j)
        ++current_node->inputs[j]->num_uses;
    }
    // Graph outputs are roots: keep them alive
    for (uint32_t i = 0; i < num_outputs_; ++i) {
      if (!(nodes_[output_ids_[i].raw()]->flags.test(NodeFlags::DEAD)))
        ++nodes_[output_ids_[i].raw()]->num_uses;
    }
  }

  Arena arena_;
  ExprPool* pool_;
  SymbolTable* tab_;

  GraphNode** nodes_;
  SlotId** input_slots_;   // [node_id] → per-node input slot ID array (or nullptr)
  SlotId** output_slots_;  // [node_id] → per-node output slot ID array (or nullptr)
  // num_nodes_ is monotonic-by-increment: alloc_node_ is the only mutator
  // (calls .bump() once per allocation; never decreases or resets).  The
  // Monotonic wrapper makes the discipline explicit at the type level —
  // any future code that tries to assign or decrement the counter fails
  // to compile.  Bonus: bump()'s contract catches uint32_t wraparound.
  // sizeof(Monotonic<uint32_t>) == sizeof(uint32_t) per Mutation.h
  // static_assert; layout-preserving.
  safety::Monotonic<uint32_t> num_nodes_{0};
  uint32_t capacity_;

  NodeId* input_ids_;
  uint32_t num_inputs_;
  NodeId* output_ids_;
  uint32_t num_outputs_;
};

} // namespace crucible
