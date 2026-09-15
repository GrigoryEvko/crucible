#pragma once

// Mutable computation graph for kernel scheduling.  Three types carry it:
//   GraphNode — one operation producing output buffers
//   Inst      — one micro-op in SSA form, the unit of a kernel body
//   Graph     — arena-owned container with the transforms
//
// A kernel body is an explicit micro-op DAG rather than a closure, so it is
// inspectable, serializable and directly emittable as device source.

#include <crucible/Arena.h>
#include <crucible/CKernel.h>
#include <crucible/Expr.h>
#include <crucible/Platform.h>
#include <crucible/fixy/Wrap.h>
#include <crucible/safety/Decide.h>
#include <crucible/safety/Post.h>
#include <crucible/safety/Pre.h>

#include <bit>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <span>

namespace crucible {

class ExprPool;
class SymbolTable;

enum class NodeKind : uint8_t {
    INPUT,  // Graph input (no computation)
    CONSTANT,  // Compile-time constant tensor
    POINTWISE,  // Element-wise computation
    REDUCTION,  // Reduction (sum, max, argmax, etc.)
    SCAN,  // Prefix scan (cumsum, cumprod)
    SORT,  // Sort operation
    EXTERN,  // Opaque external kernel (mm, conv, cuBLAS)
    TEMPLATE,  // Template-based kernel (CUTLASS, Triton)
    MUTATION,  // In-place mutation of existing buffer
    NOP,  // No computation (concat, view, etc.)
    // Sentinel: the count of valid kinds, never a valid kind itself.  It
    // sizes per-kind lookup tables without a magic number.
    NUM_KINDS,
};

enum class ReduceOp : uint8_t {
    SUM,
    PROD,
    MAX,
    MIN,
    ARGMAX,
    ARGMIN,
    ANY,
    XOR_SUM,
    WELFORD,
    DOT,
    NUM_OPS,  // Sentinel, like NodeKind::NUM_KINDS.
};

// How two adjacent ops share their intermediate, which is the same thing as
// where that intermediate lives.  The cost model reads the kind to pick the
// effective bandwidth for the edge.
enum class FuseKind : uint8_t {
    NONE,  // Cannot fuse — intermediate goes through HBM
    REGISTER,  // Same iteration space: intermediate in registers
    SMEM,  // Same block, different iteration: intermediate via smem
    EPILOGUE,  // EXTERN output stays in accumulator, epilogue applied
    PROLOGUE,  // Input transformed in registers before EXTERN kernel
    BROADCAST,  // Reduction output broadcast to consumers via smem
    NUM_KINDS,  // Sentinel, like NodeKind::NUM_KINDS.
};

enum class ReduceHint : uint8_t {
    DEFAULT,
    INNER,
    OUTER
};

// Worn through a typed bit-field so two unrelated flag enums cannot be
// mixed on the same byte.  The underlying uint8_t keeps the one-byte slot
// the hand-packed GraphNode layout budgets for it.
enum class NodeFlags : std::uint8_t {
    DEAD = 1 << 0,
    VISITED = 1 << 1,
    FUSED = 1 << 2,
    REALIZED = 1 << 3,
};

[[nodiscard]] inline NodeKind classify_node_kind(CKernelId kid) {
    // The specific overrides must precede the range checks below, which
    // would otherwise swallow them.
    if (kid == CKernelId::REDUCE_CUMSUM || kid == CKernelId::ASSOC_SCAN) return NodeKind::SCAN;
    if (kid == CKernelId::COPY_) return NodeKind::MUTATION;

    // Activations and elementwise ops occupy one contiguous enum range.
    if (kid >= CKernelId::ACT_RELU && kid <= CKernelId::EWISE_FILL) return NodeKind::POINTWISE;

    if (kid >= CKernelId::REDUCE_SUM && kid <= CKernelId::REDUCE_TOPK) return NodeKind::REDUCTION;

    // Data movement rewrites metadata and computes nothing.
    if (kid >= CKernelId::VIEW && kid <= CKernelId::UNFOLD) return NodeKind::NOP;

    return NodeKind::EXTERN;
}

enum class MicroOp : uint8_t {
    LOAD,
    STORE,

    // Arithmetic
    ADD,
    SUB,
    MUL,
    TRUEDIV,
    FLOORDIV,
    MOD,
    NEG,
    ABS,
    RECIPROCAL,
    SQUARE,

    // Comparison
    EQ,
    NE,
    LT,
    LE,
    GT,
    GE,

    // Math
    EXP,
    LOG,
    LOG2,
    SQRT,
    RSQRT,
    SIN,
    COS,
    TAN,
    ASIN,
    ACOS,
    ATAN,
    SINH,
    COSH,
    TANH,
    ASINH,
    ERF,
    CEIL,
    FLOOR,
    TRUNC,
    ROUND,
    SIGMOID,
    RELU,

    // Bitwise
    BIT_AND,
    BIT_OR,
    BIT_XOR,
    BIT_NOT,
    LSHIFT,
    RSHIFT,

    // Logic
    AND,
    OR,
    NOT,

    // Special
    TO_DTYPE,  // operands[0]=value, target dtype in aux
    CONSTANT,  // Immediate value in aux (int64_t or bitcast double)
    WHERE,  // operands = {cond, true_val, false_val}
    REDUCE,  // operands[0]=value, accumulated by owning node's reduce_op
    INDEX_EXPR,  // Symbolic index (Expr* bit-cast into aux)
    NUM_OPS,  // Sentinel, like NodeKind::NUM_KINDS.
};

// An SSA operand reference into the enclosing ComputeBody's ops array.
// Strongly typed so it cannot be confused with the body's other uint16_t
// scalars, which sit in the same struct and would otherwise swap silently.
//
// It stays an aggregate over one uint16_t, which keeps Inst at 8 bytes and
// keeps brace-initialization of an Inst's operands array working.
struct InstIndex {
    uint16_t v = 0;

    constexpr bool operator==(const InstIndex&) const noexcept = default;
    constexpr auto operator<=>(const InstIndex&) const noexcept = default;

    [[nodiscard, gnu::const]] constexpr uint16_t raw() const noexcept { return v; }
};

static_assert(sizeof(InstIndex) == sizeof(uint16_t), "InstIndex must stay 2 bytes to preserve sizeof(Inst) == 8");
static_assert(std::is_standard_layout_v<InstIndex>);

// One micro-op in SSA form.  Operand slots are used as:
//   LOAD:   operands[0] = input buffer index
//   Unary:  operands[0] = source
//   Binary: operands[0] = left, operands[1] = right
//   WHERE:  operands[0] = condition, [1] = true value, [2] = false value
struct Inst {
    MicroOp op{};  // Zero is LOAD, always overwritten before use.
    ScalarType dtype = ScalarType::Undefined;
    InstIndex operands[3]{};
};

static_assert(sizeof(Inst) == 8, "Inst must be 8 bytes");
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(Inst);

// A kernel body as a flat array of SSA instructions, directly emittable as
// device source.  For C = relu(A + B):
//   [0] LOAD  buf0, idx
//   [1] LOAD  buf1, idx
//   [2] ADD   $0, $1
//   [3] RELU  $2
//   [4] STORE $3
struct ComputeBody {
    Inst* ops = nullptr;
    uint16_t num_ops = 0;
    uint16_t num_loads = 0;  // Also the number of distinct input buffers.
    uint16_t store_op = 0;
    uint16_t pad = 0;
    // Per-instruction auxiliary data, null until the body holds its first
    // CONSTANT, TO_DTYPE or INDEX_EXPR.  Those three carry the constant's
    // value, the target dtype and the bit-cast Expr pointer respectively.
    int64_t* aux = nullptr;
};

struct ExternInfo {
    const char* python_kernel_name = nullptr;  // e.g., "aten.mm.default"
    const char* cpp_kernel_name = nullptr;  // e.g., "at::mm"
    int64_t* constant_args = nullptr;
    uint16_t num_constant_args = 0;
};

// One computation, hand-packed into a single cache line with no padding
// waste.  The static_assert below holds the layout.
//
// A REDUCTION node concatenates two ranges into one size array:
//   size[0 .. ndim-1]            = output ranges
//   size[ndim .. ndim+nred-1]    = reduction ranges
struct GraphNode {
    NodeId id;  // Also names the output buffer.
    NodeKind kind = NodeKind::NOP;
    fixy::wrap::Bits<NodeFlags> flags{};
    uint8_t ndim = 0;
    uint8_t nred = 0;  // Zero for anything but a reduction.

    ScalarType dtype = ScalarType::Undefined;
    ScalarType src_dtype = ScalarType::Undefined;  // Reductions only.
    int8_t device_idx = -1;
    ReduceOp reduce_op{};  // Meaningful only for a REDUCTION.
    ReduceHint reduce_hint{};
    uint8_t pad0 = 0;
    uint16_t num_inputs = 0;

    const Expr** size = nullptr;
    const Expr** stride = nullptr;  // Null until the scheduler freezes layout.
    void* body = nullptr;
    GraphNode** inputs = nullptr;

    uint16_t num_uses = 0;  // Live consumer count, driving dead-code removal.
    uint16_t num_outputs = 1;
    uint32_t schedule_order = 0;
    uint32_t group_hash = 0;  // Hash of device and ranges, for fusion.
    uint32_t fused_group_id = 0;  // Zero means unfused.

    [[nodiscard]] bool is_dead() const { return flags.test(NodeFlags::DEAD); }

    // device_idx is sentinel-encoded, with -1 for the host.  Reading the
    // raw field makes every caller remember that, and `device_idx < 0`
    // reads like a bug rather than a host check.
    static constexpr int8_t kCpuDeviceIdx = -1;

    [[nodiscard, gnu::pure]] bool is_cpu() const noexcept { return device_idx == kCpuDeviceIdx; }
    [[nodiscard, gnu::pure]] bool is_gpu() const noexcept { return device_idx >= 0; }
    // The precondition is what stops a host node from returning its -1
    // sentinel reinterpreted as a large unsigned device index.
    [[nodiscard, gnu::pure]] uint8_t gpu_idx() const noexcept pre(is_gpu()) { return static_cast<uint8_t>(device_idx); }

    // The positive-nred precondition is what keeps the size + ndim offset
    // inside the reduction tail instead of one past the array end.
    [[nodiscard]] const Expr** reduction_ranges() const CRUCIBLE_LIFETIMEBOUND pre(kind == NodeKind::REDUCTION)
        pre(::crucible::decide::positive(nred)) {
        return size + ndim;
    }

    // How `body` is interpreted depends on `kind`:
    //   POINTWISE, REDUCTION, SCAN            → ComputeBody
    //   EXTERN, TEMPLATE                      → ExternInfo
    //   INPUT, CONSTANT, MUTATION, NOP, SORT  → unused, null
    //
    // The preconditions on the two accessors are what reject a mismatch.
    // Without them a wrong-kind call would reinterpret arena memory as the
    // other struct and read garbage out of it.  The predicates let a caller
    // branch instead of guessing.
    [[nodiscard, gnu::pure]] bool has_compute_body() const noexcept {
        return kind == NodeKind::POINTWISE || kind == NodeKind::REDUCTION || kind == NodeKind::SCAN;
    }
    [[nodiscard, gnu::pure]] bool has_extern_info() const noexcept {
        return kind == NodeKind::EXTERN || kind == NodeKind::TEMPLATE;
    }

    [[nodiscard]] ComputeBody* compute_body() const CRUCIBLE_LIFETIMEBOUND pre(has_compute_body())
        pre(body != nullptr) {
        return static_cast<ComputeBody*>(body);
    }

    [[nodiscard]] ExternInfo* extern_info() const CRUCIBLE_LIFETIMEBOUND pre(has_extern_info()) pre(body != nullptr) {
        return static_cast<ExternInfo*>(body);
    }
};

static_assert(sizeof(GraphNode) == 64, "GraphNode must be 64 bytes");
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(GraphNode);

// Every allocation comes from the arena and dies with the Graph.  Nodes sit
// in a flat array indexed by id, so nodes_[id] is that node.
class CRUCIBLE_OWNER Graph {
public:
    [[gnu::cold]] explicit Graph(effects::Alloc a, ExprPool* pool, SymbolTable* tab = nullptr)
        : pool_(pool),
          tab_(tab),
          nodes_(nullptr),
          input_slots_(nullptr),
          output_slots_(nullptr),
          capacity_(0),
          input_ids_(nullptr),
          num_inputs_(0),
          output_ids_(nullptr),
          num_outputs_(0) {
        grow_(a, 1024);
    }

    Graph(const Graph&) = delete("Graph owns an arena; copy would alias or double-free");
    Graph& operator=(const Graph&) = delete("Graph owns an arena; copy would alias or double-free");
    Graph(Graph&&) = delete("interior GraphNode* pointers into arena would dangle");
    Graph& operator=(Graph&&) = delete("interior GraphNode* pointers into arena would dangle");

    [[nodiscard]] GraphNode* add_input(effects::Alloc a, ScalarType dtype, int8_t device_idx,
                                       std::span<const Expr* const> size) {
        GraphNode* n = alloc_node_(a);
        n->kind = NodeKind::INPUT;
        n->dtype = dtype;
        n->device_idx = device_idx;
        n->ndim = static_cast<uint8_t>(size.size());
        n->size = copy_exprs_(a, size);
        return n;
    }

    [[nodiscard]] GraphNode* add_pointwise(effects::Alloc a, std::span<const Expr* const> ranges, ScalarType dtype,
                                           int8_t device_idx, ComputeBody* body, std::span<GraphNode* const> inputs) {
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

    [[nodiscard]] GraphNode* add_reduction(effects::Alloc a, std::span<const Expr* const> ranges,
                                           std::span<const Expr* const> red_ranges, ReduceOp reduce_op, ReduceHint hint,
                                           ScalarType dtype, ScalarType src_dtype, int8_t device_idx, ComputeBody* body,
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
        // Both copies are guarded by size because memcpy with a null source
        // is undefined even at length zero, and both an empty span and a
        // zero-length arena allocation hand back null.
        if (!ranges.empty()) {
            std::memcpy(n->size, ranges.data(), ranges.size_bytes());
        }
        if (!red_ranges.empty()) {
            std::memcpy(n->size + n->ndim, red_ranges.data(), red_ranges.size_bytes());
        }
        n->body = body;
        set_inputs_(a, n, inputs);
        return n;
    }

    [[nodiscard]] GraphNode* add_extern(effects::Alloc a, const char* py_name, const char* cpp_name, ScalarType dtype,
                                        int8_t device_idx, std::span<const Expr* const> size,
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
            std::memcpy(info->constant_args, constant_args.data(), constant_args.size_bytes());
        } else {
            info->constant_args = nullptr;
        }
        n->body = info;
        set_inputs_(a, n, inputs);
        return n;
    }

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

    // The aux array is allocated on demand, since only CONSTANT, TO_DTYPE
    // and INDEX_EXPR use it.
    void alloc_body_aux(effects::Alloc a, ComputeBody* body) {
        if (!body->aux) {
            body->aux = arena_.alloc_array<int64_t>(a, body->num_ops);
            std::memset(body->aux, 0, body->num_ops * sizeof(int64_t));
        }
    }

    void set_graph_inputs(effects::Alloc a, std::span<const NodeId> ids) {
        num_inputs_ = static_cast<uint32_t>(ids.size());
        if (ids.empty()) {
            input_ids_ = nullptr;
            return;
        }
        input_ids_ = arena_.alloc_array<NodeId>(a, ids.size());
        std::memcpy(input_ids_, ids.data(), ids.size_bytes());
    }

    void set_graph_outputs(effects::Alloc a, std::span<const NodeId> ids) {
        num_outputs_ = static_cast<uint32_t>(ids.size());
        if (ids.empty()) {
            output_ids_ = nullptr;
            return;
        }
        output_ids_ = arena_.alloc_array<NodeId>(a, ids.size());
        std::memcpy(output_ids_, ids.data(), ids.size_bytes());
    }

    // The slot side-tables run parallel to nodes_, indexed by node id, and
    // read null until the lowering pass fills them.  They live outside
    // GraphNode so its cache-line layout stays intact: slot ids are touched
    // during buffer allocation and emission, never during a hot traversal
    // like dead-code elimination or the topological sort.
    //
    // Every index bound in this section uses CRUCIBLE_PRE rather than a
    // pre() clause.  These bodies are a single subscripted return, and on
    // this toolchain a pre() predicate reading a member through `this` is
    // silently skipped at consteval for exactly that shape.  The macro also
    // collapses to [[assume]] under NDEBUG, which is the optimizer hint the
    // bodies need anyway.
    void set_input_slots(effects::Alloc a, NodeId node_id, std::span<const SlotId> slots) {
        CRUCIBLE_PRE(node_id.raw() < num_nodes_.get());
        if (slots.empty()) {
            input_slots_[node_id.raw()] = nullptr;
            return;
        }
        input_slots_[node_id.raw()] = arena_.alloc_array<SlotId>(a, slots.size());
        std::memcpy(input_slots_[node_id.raw()], slots.data(), slots.size_bytes());

        // Reaching here means the span was non-empty, so the slot must hold
        // the allocation just installed.  This catches a refactor that drops
        // the assignment and leaves the slot at whatever it held before.
        CRUCIBLE_POST(0, input_slots_[node_id.raw()] != nullptr);
    }

    void set_output_slots(effects::Alloc a, NodeId node_id, std::span<const SlotId> slots) {
        CRUCIBLE_PRE(node_id.raw() < num_nodes_.get());
        if (slots.empty()) {
            output_slots_[node_id.raw()] = nullptr;
            return;
        }
        output_slots_[node_id.raw()] = arena_.alloc_array<SlotId>(a, slots.size());
        std::memcpy(output_slots_[node_id.raw()], slots.data(), slots.size_bytes());

        CRUCIBLE_POST(0, output_slots_[node_id.raw()] != nullptr);
    }

    [[nodiscard]] const SlotId* input_slots(NodeId node_id) const CRUCIBLE_LIFETIMEBOUND {
        CRUCIBLE_PRE(node_id.raw() < num_nodes_.get());
        return input_slots_[node_id.raw()];
    }

    [[nodiscard]] const SlotId* output_slots(NodeId node_id) const CRUCIBLE_LIFETIMEBOUND {
        CRUCIBLE_PRE(node_id.raw() < num_nodes_.get());
        return output_slots_[node_id.raw()];
    }

    // Scans every live node, patches its inputs array and adjusts the use
    // counts on both nodes.
    void replace_all_uses(GraphNode* old_node, GraphNode* new_node) {
        if (old_node == new_node) return;
        const uint32_t n_nodes = num_nodes_.get();
        for (uint32_t i = 0; i < n_nodes; ++i) {
            GraphNode* n = nodes_[i];
            if (n->flags.test(NodeFlags::DEAD)) continue;
            for (uint16_t j = 0; j < n->num_inputs; ++j) {
                if (n->inputs[j] == old_node) {
                    n->inputs[j] = new_node;
                    --old_node->num_uses;
                    ++new_node->num_uses;
                }
            }
        }
        for (uint32_t i = 0; i < num_outputs_; ++i) {
            if (output_ids_[i] == old_node->id) output_ids_[i] = new_node->id;
        }
    }

    // Marks every node with no uses and no side effects dead, and iterates:
    // killing a node decrements its inputs' use counts, which can kill them
    // in turn.
    //
    // On return, every live non-mutating node either has a consumer or is a
    // graph output.  The later passes rely on that.
    void eliminate_dead_nodes() noexcept {
        recompute_uses_();
        bool changed = true;
        while (changed) {
            changed = false;
            for (uint32_t i = num_nodes_.get(); i-- > 0;) {
                GraphNode* current_node = nodes_[i];
                if (current_node->flags.test(NodeFlags::DEAD)) continue;
                if (current_node->num_uses == 0 && current_node->kind != NodeKind::MUTATION) {
                    current_node->flags.set(NodeFlags::DEAD);
                    for (uint16_t j = 0; j < current_node->num_inputs; ++j)
                        --current_node->inputs[j]->num_uses;
                    changed = true;
                }
            }
        }
    }

    // Kahn's algorithm over a flat successor array built from the nodes'
    // input lists.  Sets schedule_order on every live node.
    void topological_sort(effects::Alloc a) {
        const uint32_t n_nodes = num_nodes_.get();
        auto* in_deg = arena_.alloc_array<uint32_t>(a, n_nodes);
        auto* succ_cnt = arena_.alloc_array<uint32_t>(a, n_nodes);
        std::memset(in_deg, 0, n_nodes * sizeof(uint32_t));
        std::memset(succ_cnt, 0, n_nodes * sizeof(uint32_t));

        uint32_t total_edges = 0;
        for (uint32_t i = 0; i < n_nodes; ++i) {
            GraphNode* current_node = nodes_[i];
            if (current_node->flags.test(NodeFlags::DEAD)) continue;
            for (uint16_t j = 0; j < current_node->num_inputs; ++j) {
                GraphNode* dep_node = current_node->inputs[j];
                if (!(dep_node->flags.test(NodeFlags::DEAD))) {
                    ++in_deg[i];
                    ++succ_cnt[dep_node->id.raw()];
                    ++total_edges;
                }
            }
        }

        // Prefix-sum the successor counts into offsets, then scatter.
        auto* offset = arena_.alloc_array<uint32_t>(a, n_nodes + 1);
        offset[0] = 0;
        for (uint32_t i = 0; i < n_nodes; ++i)
            offset[i + 1] = offset[i] + succ_cnt[i];

        auto* succs = arena_.alloc_array<uint32_t>(a, total_edges > 0 ? total_edges : 1);
        std::memset(succ_cnt, 0, n_nodes * sizeof(uint32_t));
        for (uint32_t i = 0; i < n_nodes; ++i) {
            GraphNode* current_node = nodes_[i];
            if (current_node->flags.test(NodeFlags::DEAD)) continue;
            for (uint16_t j = 0; j < current_node->num_inputs; ++j) {
                uint32_t dep_id = current_node->inputs[j]->id.raw();
                if (!(nodes_[dep_id]->flags.test(NodeFlags::DEAD))) succs[offset[dep_id] + succ_cnt[dep_id]++] = i;
            }
        }

        auto* queue = arena_.alloc_array<uint32_t>(a, n_nodes);
        uint32_t head = 0, tail = 0;
        for (uint32_t i = 0; i < n_nodes; ++i) {
            if (!(nodes_[i]->flags.test(NodeFlags::DEAD)) && in_deg[i] == 0) queue[tail++] = i;
        }

        uint32_t next_schedule_order = 0;
        while (head < tail) {
            uint32_t dequeued_node_id = queue[head++];
            nodes_[dequeued_node_id]->schedule_order = next_schedule_order++;
            for (uint32_t k = offset[dequeued_node_id]; k < offset[dequeued_node_id + 1]; ++k) {
                if (--in_deg[succs[k]] == 0) queue[tail++] = succs[k];
            }
        }
    }

    // Replaces structurally identical nodes with their first occurrence and
    // returns how many were eliminated.  Topological order guarantees every
    // input is already canonicalized when its consumer is examined.
    //
    // The hash pass resolves inputs through canonical_representative rather
    // than rewriting pointers as it goes.  One sweep at the end patches
    // every live node, which is what keeps this linear instead of running a
    // full use-replacement scan per elimination.
    [[nodiscard]] uint32_t eliminate_common_subexpressions(effects::Alloc a) {
        topological_sort(a);

        const uint32_t n_nodes = num_nodes_.get();

        // Starts as the identity map and gains an entry per duplicate found.
        auto* canonical_representative = arena_.alloc_array<GraphNode*>(a, n_nodes);
        for (uint32_t i = 0; i < n_nodes; ++i)
            canonical_representative[i] = nodes_[i];

        // schedule_order is dense over the live nodes, so the ordered list
        // comes from a single scatter rather than a sort.
        uint32_t num_live_nodes = 0;
        for (uint32_t i = 0; i < n_nodes; ++i)
            if (!(nodes_[i]->flags.test(NodeFlags::DEAD))) ++num_live_nodes;
        auto* topological_order = arena_.alloc_array<GraphNode*>(a, num_live_nodes > 0 ? num_live_nodes : 1);
        for (uint32_t i = 0; i < n_nodes; ++i) {
            if (!(nodes_[i]->flags.test(NodeFlags::DEAD))) topological_order[nodes_[i]->schedule_order] = nodes_[i];
        }

        // Open addressing at roughly half load.
        uint32_t cse_table_capacity = std::bit_ceil(num_live_nodes * 2 + 1);
        auto* cse_table_hashes = arena_.alloc_array<uint64_t>(a, cse_table_capacity);
        auto* cse_table_nodes = arena_.alloc_array<GraphNode*>(a, cse_table_capacity);
        std::memset(cse_table_nodes, 0, cse_table_capacity * sizeof(GraphNode*));

        uint32_t eliminated_count = 0;
        uint32_t cse_table_index_mask = cse_table_capacity - 1;
        for (uint32_t i = 0; i < num_live_nodes; ++i) {
            GraphNode* current_node = topological_order[i];
            if (current_node->kind == NodeKind::INPUT || current_node->kind == NodeKind::MUTATION) continue;

            uint64_t node_cse_hash = cse_hash_(current_node, canonical_representative);

            for (uint32_t probe_iteration = 0; probe_iteration < cse_table_capacity; ++probe_iteration) {
                uint32_t probe_slot_index =
                    (static_cast<uint32_t>(node_cse_hash) + probe_iteration) & cse_table_index_mask;
                if (!cse_table_nodes[probe_slot_index]) {
                    cse_table_hashes[probe_slot_index] = node_cse_hash;
                    cse_table_nodes[probe_slot_index] = current_node;
                    break;
                }
                if (cse_table_hashes[probe_slot_index] == node_cse_hash
                    && cse_equal_(current_node, cse_table_nodes[probe_slot_index], canonical_representative)) {
                    canonical_representative[current_node->id.raw()] = cse_table_nodes[probe_slot_index];
                    current_node->flags.set(NodeFlags::DEAD);
                    ++eliminated_count;
                    break;
                }
            }
        }

        if (eliminated_count > 0) {
            for (uint32_t i = 0; i < n_nodes; ++i) {
                GraphNode* current_node = nodes_[i];
                if (current_node->flags.test(NodeFlags::DEAD)) continue;
                for (uint16_t j = 0; j < current_node->num_inputs; ++j)
                    current_node->inputs[j] = canonical_representative[current_node->inputs[j]->id.raw()];
            }
            for (uint32_t i = 0; i < num_outputs_; ++i)
                output_ids_[i] = canonical_representative[output_ids_[i].raw()]->id;
            recompute_uses_();
        }
        return eliminated_count;
    }

    // Assigns a fusion group to every node that can share a kernel launch,
    // so the producer's output stays in registers or shared memory instead
    // of round-tripping through device memory.  Returns the group count.
    //
    // The pass is greedy in topological order: a fusible node joins the
    // group of its first compatible input, or starts a new group.
    // Compatible means the same device and the same output ranges, and
    // ranges compare by pointer because the expressions are interned.
    //
    // It also leaves group_hash on each node so later passes can reject an
    // incompatible pair without walking the ranges.
    [[nodiscard]] uint32_t compute_fusion_groups(effects::Alloc a) {
        topological_sort(a);

        const uint32_t n_nodes = num_nodes_.get();

        uint32_t num_live_nodes = 0;
        for (uint32_t i = 0; i < n_nodes; ++i)
            if (!(nodes_[i]->flags.test(NodeFlags::DEAD))) ++num_live_nodes;
        auto* topological_order = arena_.alloc_array<GraphNode*>(a, num_live_nodes > 0 ? num_live_nodes : 1);
        for (uint32_t i = 0; i < n_nodes; ++i) {
            if (!(nodes_[i]->flags.test(NodeFlags::DEAD))) topological_order[nodes_[i]->schedule_order] = nodes_[i];
        }

        for (uint32_t i = 0; i < n_nodes; ++i) {
            nodes_[i]->fused_group_id = 0;
            nodes_[i]->group_hash = 0;
        }

        // The hash covers device and ranges only.  Two nodes whose hashes
        // differ can never fuse.  The field selection is deliberate and
        // load-bearing: folding in kind or dtype would stop nodes of
        // different kinds from ever grouping, which is exactly what this
        // pass exists to do.
        for (uint32_t i = 0; i < num_live_nodes; ++i) {
            GraphNode* current_node = topological_order[i];
            uint64_t node_group_hash =
                detail::fmix64(static_cast<uint64_t>(static_cast<uint8_t>(current_node->device_idx))
                               | (static_cast<uint64_t>(current_node->ndim) << 8));
            for (uint8_t d = 0; d < current_node->ndim; ++d)
                node_group_hash = detail::combine_ids(node_group_hash, std::bit_cast<uint64_t>(current_node->size[d]));
            current_node->group_hash = static_cast<uint32_t>(node_group_hash);
        }

        uint32_t next_fusion_group_id = 1;
        for (uint32_t i = 0; i < num_live_nodes; ++i) {
            GraphNode* current_node = topological_order[i];
            if (!is_fusible_(current_node->kind)) continue;

            for (uint16_t j = 0; j < current_node->num_inputs; ++j) {
                GraphNode* input_node = current_node->inputs[j];
                if (input_node->fused_group_id == 0) continue;
                if (!is_fusible_(input_node->kind)) continue;
                if (input_node->group_hash != current_node->group_hash) continue;
                // The hash can collide, so confirm the ranges themselves.
                if (ranges_compatible_(current_node, input_node)) {
                    current_node->fused_group_id = input_node->fused_group_id;
                    current_node->flags.set(NodeFlags::FUSED);
                    break;
                }
            }
            if (current_node->fused_group_id == 0) current_node->fused_group_id = next_fusion_group_id++;
        }
        return next_fusion_group_id - 1;
    }

    [[nodiscard, gnu::pure]] uint32_t group_size(uint32_t group_id) const noexcept {
        uint32_t match_count = 0;
        const uint32_t n_nodes = num_nodes_.get();
        for (uint32_t i = 0; i < n_nodes; ++i)
            if (nodes_[i]->fused_group_id == group_id) ++match_count;
        return match_count;
    }

    void clear_visited() {
        const uint32_t n_nodes = num_nodes_.get();
        for (uint32_t i = 0; i < n_nodes; ++i)
            nodes_[i]->flags.unset(NodeFlags::VISITED);
    }

    [[nodiscard, gnu::pure]] uint32_t count_live() const noexcept {
        uint32_t live_count = 0;
        const uint32_t n_nodes = num_nodes_.get();
        for (uint32_t i = 0; i < n_nodes; ++i) {
            if (!(nodes_[i]->flags.test(NodeFlags::DEAD))) ++live_count;
        }
        return live_count;
    }

    [[nodiscard, gnu::pure]] GraphNode* node(NodeId id) const noexcept CRUCIBLE_LIFETIMEBOUND {
        CRUCIBLE_PRE(id.raw() < num_nodes_.get());
        return nodes_[id.raw()];
    }

    [[nodiscard, gnu::pure]] GraphNode* node(uint32_t id) const noexcept CRUCIBLE_LIFETIMEBOUND {
        CRUCIBLE_PRE(id < num_nodes_.get());
        return nodes_[id];
    }

    [[nodiscard, gnu::pure]] uint32_t num_nodes() const noexcept { return num_nodes_.get(); }
    [[nodiscard, gnu::pure]] uint32_t num_graph_inputs() const noexcept { return num_inputs_; }
    [[nodiscard, gnu::pure]] uint32_t num_graph_outputs() const noexcept { return num_outputs_; }
    [[nodiscard, gnu::pure]] const NodeId* graph_input_ids() const noexcept CRUCIBLE_LIFETIMEBOUND {
        return input_ids_;
    }
    [[nodiscard, gnu::pure]] const NodeId* graph_output_ids() const noexcept CRUCIBLE_LIFETIMEBOUND {
        return output_ids_;
    }

    [[nodiscard, gnu::pure]] ExprPool* pool() const noexcept CRUCIBLE_LIFETIMEBOUND { return pool_; }
    [[nodiscard, gnu::pure]] SymbolTable* tab() const noexcept CRUCIBLE_LIFETIMEBOUND { return tab_; }
    [[nodiscard]] Arena& arena() noexcept CRUCIBLE_LIFETIMEBOUND { return arena_; }

private:
    GraphNode* alloc_node_(effects::Alloc a) {
        if (num_nodes_.get() >= capacity_) grow_(a, capacity_ * 2);
        auto* n = ::new(arena_.alloc_obj<GraphNode>(a)) GraphNode{};
        n->id = NodeId{num_nodes_.get()};
        nodes_[num_nodes_.get()] = n;
        num_nodes_.bump();
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
        // Zero-fill the new entries so an unset slot reads as null.
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
        if (src.empty()) return nullptr;
        auto** dst = arena_.alloc_array<const Expr*>(a, src.size());
        std::memcpy(dst, src.data(), src.size_bytes());
        return dst;
    }

    const char* copy_string_(effects::Alloc a, const char* src) {
        if (!src) return nullptr;
        size_t len = std::strlen(src) + 1;
        auto* dst = static_cast<char*>(
            arena_.alloc(a, crucible::fixy::wrap::Positive<size_t>{len}, crucible::fixy::wrap::PowerOfTwo<size_t>{1}));
        std::memcpy(dst, src, len);
        return dst;
    }

    // Written as an exhaustive switch so a newly added NodeKind trips
    // -Werror=switch here instead of silently classifying as non-fusible.
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

    // Range expressions are interned, so equality per dimension is a
    // pointer comparison.
    [[nodiscard]] static bool ranges_compatible_(const GraphNode* lhs_node, const GraphNode* rhs_node) {
        if (lhs_node->device_idx != rhs_node->device_idx) return false;
        if (lhs_node->ndim != rhs_node->ndim) return false;
        for (uint8_t d = 0; d < lhs_node->ndim; ++d)
            if (lhs_node->size[d] != rhs_node->size[d]) return false;
        return true;
    }

    // Structural hash for the elimination pass.  Inputs resolve through the
    // canonical map so no pointer has to be rewritten mid-pass.
    //
    // The value is process-local.  It mixes arena addresses, which are
    // randomized per process, so it must never be persisted or used as a
    // durable cache key.  Probing within one compile pass is its only job.
    //
    // The field selection is deliberate and load-bearing, not an
    // optimization.  Folding in every member would include `id`, which is
    // unique per node and would defeat the pass entirely, along with scratch
    // fields that later passes overwrite.  Two structurally equivalent nodes
    // would then hash differently, miss the collapse, and produce a graph
    // that looks valid and is wrong.
    [[nodiscard]] static uint64_t cse_hash_(const GraphNode* node, const GraphNode* const* canonical) {
        uint64_t structural_hash =
            detail::fmix64(static_cast<uint64_t>(std::to_underlying(node->kind))
                           | (static_cast<uint64_t>(std::to_underlying(node->dtype)) << 8)
                           | (static_cast<uint64_t>(static_cast<uint8_t>(node->device_idx)) << 16)
                           | (static_cast<uint64_t>(node->ndim) << 24) | (static_cast<uint64_t>(node->nred) << 32));

        // Size expressions are interned, so their addresses are identities.
        const auto total_dims = static_cast<uint8_t>(node->ndim + node->nred);
        for (uint8_t d = 0; d < total_dims; ++d)
            structural_hash = detail::combine_ids(structural_hash, std::bit_cast<uint64_t>(node->size[d]));

        for (uint16_t j = 0; j < node->num_inputs; ++j)
            structural_hash =
                detail::combine_ids(structural_hash, std::bit_cast<uint64_t>(canonical[node->inputs[j]->id.raw()]));

        if ((node->kind == NodeKind::POINTWISE || node->kind == NodeKind::REDUCTION) && node->body) {
            auto* body = node->compute_body();
            // Inst is trivially copyable and exactly one word, so each
            // instruction folds in as a single value.
            static_assert(sizeof(Inst) == 8);
            for (uint16_t k = 0; k < body->num_ops; ++k) {
                uint64_t packed_inst;
                std::memcpy(&packed_inst, &body->ops[k], 8);
                structural_hash = detail::combine_ids(structural_hash, packed_inst);
            }
        }

        if (node->kind == NodeKind::EXTERN && node->body) {
            auto* info = node->extern_info();
            if (info->python_kernel_name)
                for (const char* char_cursor = info->python_kernel_name; *char_cursor; ++char_cursor)
                    structural_hash = detail::combine_ids(structural_hash, static_cast<uint64_t>(*char_cursor));
        }

        if (node->kind == NodeKind::REDUCTION)
            structural_hash ^= detail::fmix64(static_cast<uint64_t>(std::to_underlying(node->reduce_op)));

        return structural_hash;
    }

    // Structural equality, resolving inputs through the canonical map.
    [[nodiscard]] static bool cse_equal_(const GraphNode* lhs_node, const GraphNode* rhs_node,
                                         const GraphNode* const* canonical) {
        if (lhs_node->kind != rhs_node->kind || lhs_node->dtype != rhs_node->dtype
            || lhs_node->device_idx != rhs_node->device_idx || lhs_node->ndim != rhs_node->ndim
            || lhs_node->nred != rhs_node->nred || lhs_node->num_inputs != rhs_node->num_inputs)
            return false;

        // Sizes are interned, so they compare by address.
        const auto total_dims = static_cast<uint8_t>(lhs_node->ndim + lhs_node->nred);
        for (uint8_t d = 0; d < total_dims; ++d)
            if (lhs_node->size[d] != rhs_node->size[d]) return false;

        for (uint16_t j = 0; j < lhs_node->num_inputs; ++j)
            if (canonical[lhs_node->inputs[j]->id.raw()] != canonical[rhs_node->inputs[j]->id.raw()]) return false;

        if (lhs_node->kind == NodeKind::POINTWISE || lhs_node->kind == NodeKind::REDUCTION) {
            auto* lhs_body = lhs_node->compute_body();
            auto* rhs_body = rhs_node->compute_body();
            if (lhs_body != rhs_body) {
                if (!lhs_body || !rhs_body || lhs_body->num_ops != rhs_body->num_ops) return false;
                if (std::memcmp(lhs_body->ops, rhs_body->ops, lhs_body->num_ops * sizeof(Inst)) != 0) return false;
                if (lhs_body->aux != rhs_body->aux) {
                    if (!lhs_body->aux || !rhs_body->aux) return false;
                    if (std::memcmp(lhs_body->aux, rhs_body->aux, lhs_body->num_ops * sizeof(int64_t)) != 0)
                        return false;
                }
            }
        }

        if (lhs_node->kind == NodeKind::REDUCTION) {
            if (lhs_node->reduce_op != rhs_node->reduce_op || lhs_node->reduce_hint != rhs_node->reduce_hint
                || lhs_node->src_dtype != rhs_node->src_dtype)
                return false;
        }

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
                if (lhs_info->num_constant_args > 0
                    && std::memcmp(lhs_info->constant_args, rhs_info->constant_args,
                                   lhs_info->num_constant_args * sizeof(int64_t))
                           != 0)
                    return false;
            }
        }

        return true;
    }

    // Recomputes every use count from scratch, discarding stale ones.
    void recompute_uses_() {
        const uint32_t n_nodes = num_nodes_.get();
        for (uint32_t i = 0; i < n_nodes; ++i)
            nodes_[i]->num_uses = 0;

        for (uint32_t i = 0; i < n_nodes; ++i) {
            GraphNode* current_node = nodes_[i];
            if (current_node->flags.test(NodeFlags::DEAD)) continue;
            for (uint16_t j = 0; j < current_node->num_inputs; ++j)
                ++current_node->inputs[j]->num_uses;
        }
        // Graph outputs are roots, so give each one a use of its own.
        for (uint32_t i = 0; i < num_outputs_; ++i) {
            if (!(nodes_[output_ids_[i].raw()]->flags.test(NodeFlags::DEAD))) ++nodes_[output_ids_[i].raw()]->num_uses;
        }
    }

    Arena arena_;
    ExprPool* pool_;
    SymbolTable* tab_;

    GraphNode** nodes_;
    SlotId** input_slots_;  // Indexed by node id, null where unset.
    SlotId** output_slots_;  // Indexed by node id, null where unset.
    // alloc_node_ is the only mutator, and it only ever increments.
    fixy::wrap::Monotonic<uint32_t> num_nodes_{0};
    uint32_t capacity_;

    NodeId* input_ids_;
    uint32_t num_inputs_;
    NodeId* output_ids_;
    uint32_t num_outputs_;
};

}  // namespace crucible
