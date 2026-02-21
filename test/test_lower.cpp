#include <crucible/Lower.h>
#include <cassert>
#include <cstdio>
#include <cstring>

// Helper: build a TensorMeta for a 2D float tensor.
static crucible::TensorMeta make_meta(int64_t d0, int64_t d1,
                                      int8_t dev = 0,
                                      void* ptr = nullptr) {
    crucible::TensorMeta m{};
    m.ndim = 2;
    m.sizes[0] = d0;
    m.sizes[1] = d1;
    m.strides[0] = d1;
    m.strides[1] = 1;
    m.dtype = crucible::ScalarType::Float;
    m.device_type = crucible::DeviceType::CPU;
    m.device_idx = dev;
    m.layout = crucible::Layout::Strided;
    m.data_ptr = ptr;
    return m;
}

int main() {
    crucible::Arena arena(1 << 16);
    crucible::ExprPool pool;

    // ════════════════════════════════════════════════════════════════
    // Build a 3-op TraceGraph:
    //
    //   ext(slot0) ─┐
    //   ext(slot1) ─┤→ Op 0: EWISE_ADD  → slot 2
    //               └→ Op 1: ACT_RELU   → slot 3  ─┐
    //   ext(slot4)  ────────────────────────────────┤→ Op 2: GEMM_MM → slot 5
    //
    // Slots: 0,1,4 external; 2,3,5 internal.
    // DFG edges: op0→op1, op1→op2.
    // ════════════════════════════════════════════════════════════════

    constexpr uint32_t NUM_OPS = 3;
    constexpr uint32_t NUM_SLOTS = 6;

    auto* ops = arena.alloc_array<crucible::TraceEntry>(NUM_OPS);
    std::memset(ops, 0, NUM_OPS * sizeof(crucible::TraceEntry));

    // ── Op 0: EWISE_ADD(ext_slot0, ext_slot1) → slot 2 ─────────
    {
        auto& te = ops[0];
        te.schema_hash = crucible::SchemaHash{0xAA};
        te.kernel_id = crucible::CKernelId::EWISE_ADD;
        te.num_inputs = 2;
        te.num_outputs = 1;

        te.input_metas = arena.alloc_array<crucible::TensorMeta>(2);
        te.input_metas[0] = make_meta(4, 8);
        te.input_metas[1] = make_meta(4, 8);
        te.output_metas = arena.alloc_array<crucible::TensorMeta>(1);
        te.output_metas[0] = make_meta(4, 8);

        te.input_trace_indices = arena.alloc_array<crucible::OpIndex>(2);
        te.input_trace_indices[0] = crucible::OpIndex{}; // external
        te.input_trace_indices[1] = crucible::OpIndex{}; // external

        te.input_slot_ids = arena.alloc_array<crucible::SlotId>(2);
        te.input_slot_ids[0] = crucible::SlotId{0};
        te.input_slot_ids[1] = crucible::SlotId{1};

        te.output_slot_ids = arena.alloc_array<crucible::SlotId>(1);
        te.output_slot_ids[0] = crucible::SlotId{2};
    }

    // ── Op 1: ACT_RELU(op0_out) → slot 3 ───────────────────────
    {
        auto& te = ops[1];
        te.schema_hash = crucible::SchemaHash{0xBB};
        te.kernel_id = crucible::CKernelId::ACT_RELU;
        te.num_inputs = 1;
        te.num_outputs = 1;

        te.input_metas = arena.alloc_array<crucible::TensorMeta>(1);
        te.input_metas[0] = make_meta(4, 8);
        te.output_metas = arena.alloc_array<crucible::TensorMeta>(1);
        te.output_metas[0] = make_meta(4, 8);

        te.input_trace_indices = arena.alloc_array<crucible::OpIndex>(1);
        te.input_trace_indices[0] = crucible::OpIndex{0}; // from op 0

        te.input_slot_ids = arena.alloc_array<crucible::SlotId>(1);
        te.input_slot_ids[0] = crucible::SlotId{2};

        te.output_slot_ids = arena.alloc_array<crucible::SlotId>(1);
        te.output_slot_ids[0] = crucible::SlotId{3};
    }

    // ── Op 2: GEMM_MM(op1_out, ext_slot4) → slot 5 ─────────────
    {
        auto& te = ops[2];
        te.schema_hash = crucible::SchemaHash{0xCC};
        te.kernel_id = crucible::CKernelId::GEMM_MM;
        te.num_inputs = 2;
        te.num_outputs = 1;

        te.input_metas = arena.alloc_array<crucible::TensorMeta>(2);
        te.input_metas[0] = make_meta(4, 8);
        te.input_metas[1] = make_meta(8, 16);
        te.output_metas = arena.alloc_array<crucible::TensorMeta>(1);
        te.output_metas[0] = make_meta(4, 16);

        te.input_trace_indices = arena.alloc_array<crucible::OpIndex>(2);
        te.input_trace_indices[0] = crucible::OpIndex{1}; // from op 1
        te.input_trace_indices[1] = crucible::OpIndex{}; // external

        te.input_slot_ids = arena.alloc_array<crucible::SlotId>(2);
        te.input_slot_ids[0] = crucible::SlotId{3};
        te.input_slot_ids[1] = crucible::SlotId{4};

        te.output_slot_ids = arena.alloc_array<crucible::SlotId>(1);
        te.output_slot_ids[0] = crucible::SlotId{5};
    }

    // ── Build DFG edges: op0→op1, op1→op2 ───────────────────────
    crucible::Edge edges[2] = {
        {.src = crucible::OpIndex{0}, .dst = crucible::OpIndex{1},
         .src_port = 0, .dst_port = 0,
         .kind = crucible::EdgeKind::DATA_FLOW, .pad = 0},
        {.src = crucible::OpIndex{1}, .dst = crucible::OpIndex{2},
         .src_port = 0, .dst_port = 0,
         .kind = crucible::EdgeKind::DATA_FLOW, .pad = 0},
    };

    // ── Build TensorSlots ───────────────────────────────────────
    auto* slots = arena.alloc_array<crucible::TensorSlot>(NUM_SLOTS);
    std::memset(slots, 0, NUM_SLOTS * sizeof(crucible::TensorSlot));
    using crucible::SlotId; using crucible::OpIndex;
    using crucible::ScalarType; using crucible::DeviceType; using crucible::Layout;
    // External slots
    slots[0] = {.offset_bytes = 0, .nbytes = 128,
                .birth_op = OpIndex{0}, .death_op = OpIndex{0},
                .dtype = ScalarType::Float, .device_type = DeviceType::CPU,
                .device_idx = 0, .layout = Layout::Strided,
                .is_external = true, .pad = {}, .slot_id = SlotId{0}, .pad2 = {}};
    slots[1] = {.offset_bytes = 0, .nbytes = 128,
                .birth_op = OpIndex{0}, .death_op = OpIndex{0},
                .dtype = ScalarType::Float, .device_type = DeviceType::CPU,
                .device_idx = 0, .layout = Layout::Strided,
                .is_external = true, .pad = {}, .slot_id = SlotId{1}, .pad2 = {}};
    slots[4] = {.offset_bytes = 0, .nbytes = 512,
                .birth_op = OpIndex{0}, .death_op = OpIndex{2},
                .dtype = ScalarType::Float, .device_type = DeviceType::CPU,
                .device_idx = 0, .layout = Layout::Strided,
                .is_external = true, .pad = {}, .slot_id = SlotId{4}, .pad2 = {}};
    // Internal slots
    slots[2] = {.offset_bytes = 0, .nbytes = 128,
                .birth_op = OpIndex{0}, .death_op = OpIndex{1},
                .dtype = ScalarType::Float, .device_type = DeviceType::CPU,
                .device_idx = 0, .layout = Layout::Strided,
                .is_external = false, .pad = {}, .slot_id = SlotId{2}, .pad2 = {}};
    slots[3] = {.offset_bytes = 0, .nbytes = 128,
                .birth_op = OpIndex{1}, .death_op = OpIndex{2},
                .dtype = ScalarType::Float, .device_type = DeviceType::CPU,
                .device_idx = 0, .layout = Layout::Strided,
                .is_external = false, .pad = {}, .slot_id = SlotId{3}, .pad2 = {}};
    slots[5] = {.offset_bytes = 0, .nbytes = 256,
                .birth_op = OpIndex{2}, .death_op = OpIndex{2},
                .dtype = ScalarType::Float, .device_type = DeviceType::CPU,
                .device_idx = 0, .layout = Layout::Strided,
                .is_external = false, .pad = {}, .slot_id = SlotId{5}, .pad2 = {}};

    // ── Assemble TraceGraph with CSR ────────────────────────────
    auto* graph_tg = arena.alloc_obj<crucible::TraceGraph>();
    graph_tg->ops = ops;
    graph_tg->num_ops = NUM_OPS;
    graph_tg->slots = slots;
    graph_tg->num_slots = NUM_SLOTS;
    crucible::build_csr(arena, graph_tg, edges, 2, NUM_OPS);

    // ════════════════════════════════════════════════════════════════
    // Lower to Graph IR
    // ════════════════════════════════════════════════════════════════

    crucible::Graph graph(&pool);
    crucible::lower_trace_to_graph(*graph_tg, pool, graph);

    // ── Verify node count ───────────────────────────────────────
    // 3 INPUT nodes (slots 0, 1, 4) + 3 compute nodes = 6 total.
    assert(graph.num_nodes() == 6);

    // ── Verify INPUT nodes (ids 0, 1, 2) ────────────────────────
    assert(graph.node(0)->kind == crucible::NodeKind::INPUT);
    assert(graph.node(1)->kind == crucible::NodeKind::INPUT);
    assert(graph.node(2)->kind == crucible::NodeKind::INPUT);

    // INPUT output slot IDs match external slot_ids.
    using crucible::NodeId;
    assert(graph.output_slots(NodeId{0}) != nullptr);
    assert(graph.output_slots(NodeId{0})[0] == SlotId{0});
    assert(graph.output_slots(NodeId{1})[0] == SlotId{1});
    assert(graph.output_slots(NodeId{2})[0] == SlotId{4});

    // ── Verify compute nodes ────────────────────────────────────
    // Node 3: EWISE_ADD → POINTWISE
    assert(graph.node(3)->kind == crucible::NodeKind::POINTWISE);
    assert(graph.node(3)->num_inputs == 2);
    assert(graph.node(3)->ndim == 2);

    // Input slots: reads from slot 0 and slot 1
    assert(graph.input_slots(NodeId{3}) != nullptr);
    assert(graph.input_slots(NodeId{3})[0] == SlotId{0});
    assert(graph.input_slots(NodeId{3})[1] == SlotId{1});

    // Output slots: writes to slot 2
    assert(graph.output_slots(NodeId{3}) != nullptr);
    assert(graph.output_slots(NodeId{3})[0] == SlotId{2});

    // Node 4: ACT_RELU → POINTWISE
    assert(graph.node(4)->kind == crucible::NodeKind::POINTWISE);
    assert(graph.node(4)->num_inputs == 1);

    // Reads from slot 2 (output of EWISE_ADD)
    assert(graph.input_slots(NodeId{4}) != nullptr);
    assert(graph.input_slots(NodeId{4})[0] == SlotId{2});
    assert(graph.output_slots(NodeId{4})[0] == SlotId{3});

    // Node 5: GEMM_MM → EXTERN
    assert(graph.node(5)->kind == crucible::NodeKind::EXTERN);
    assert(graph.node(5)->num_inputs == 2);

    // Reads from slot 3 (relu output) and slot 4 (external weight)
    assert(graph.input_slots(NodeId{5}) != nullptr);
    assert(graph.input_slots(NodeId{5})[0] == SlotId{3});
    assert(graph.input_slots(NodeId{5})[1] == SlotId{4});
    assert(graph.output_slots(NodeId{5})[0] == SlotId{5});

    // ── Verify graph inputs: 3 INPUT nodes ──────────────────────
    assert(graph.num_graph_inputs() == 3);
    assert(graph.graph_input_ids()[0] == NodeId{0});
    assert(graph.graph_input_ids()[1] == NodeId{1});
    assert(graph.graph_input_ids()[2] == NodeId{2});

    // ── Verify graph outputs: only GEMM_MM (no DFG consumers) ───
    assert(graph.num_graph_outputs() == 1);
    assert(graph.graph_output_ids()[0] == NodeId{5});

    // ── Verify input wiring (GraphNode.inputs pointers) ─────────
    // EWISE_ADD inputs should point to INPUT nodes 0 and 1.
    assert(graph.node(3)->inputs[0] == graph.node(0));
    assert(graph.node(3)->inputs[1] == graph.node(1));

    // ACT_RELU input should point to EWISE_ADD (node 3).
    assert(graph.node(4)->inputs[0] == graph.node(3));

    // GEMM_MM inputs: ACT_RELU (node 4), external INPUT (node 2).
    assert(graph.node(5)->inputs[0] == graph.node(4));
    assert(graph.node(5)->inputs[1] == graph.node(2));

    // ── Verify symbolic sizes survive ───────────────────────────
    // GEMM_MM output is 4×16, so sizes[0] = 4, sizes[1] = 16.
    const crucible::GraphNode* mm = graph.node(5);
    assert(mm->ndim == 2);
    assert(mm->size[0]->payload == 4);
    assert(mm->size[1]->payload == 16);

    // ── Verify multi-output ops ─────────────────────────────────
    // All ops in this trace have 1 output.
    assert(graph.node(3)->num_outputs == 1);
    assert(graph.node(4)->num_outputs == 1);
    assert(graph.node(5)->num_outputs == 1);

    // ── Graph transforms still work after lowering ──────────────
    graph.topological_sort();
    // INPUT nodes should have lower schedule_order than compute nodes.
    assert(graph.node(0)->schedule_order < graph.node(3)->schedule_order);
    assert(graph.node(3)->schedule_order < graph.node(4)->schedule_order);
    assert(graph.node(4)->schedule_order < graph.node(5)->schedule_order);

    // ════════════════════════════════════════════════════════════════
    // Test: null input filtering
    // ════════════════════════════════════════════════════════════════

    // Create a single op with 3 inputs, one of which is null (bias=None).
    crucible::Arena arena2(1 << 16);
    auto* ops2 = arena2.alloc_array<crucible::TraceEntry>(1);
    std::memset(ops2, 0, sizeof(crucible::TraceEntry));

    ops2[0].schema_hash = crucible::SchemaHash{0xDD};
    ops2[0].kernel_id = crucible::CKernelId::GEMM_ADDMM;
    ops2[0].num_inputs = 3;
    ops2[0].num_outputs = 1;

    ops2[0].input_metas = arena2.alloc_array<crucible::TensorMeta>(3);
    ops2[0].input_metas[0] = make_meta(4, 8); // bias (will be null)
    ops2[0].input_metas[1] = make_meta(4, 8); // x
    ops2[0].input_metas[2] = make_meta(8, 8); // weight

    ops2[0].output_metas = arena2.alloc_array<crucible::TensorMeta>(1);
    ops2[0].output_metas[0] = make_meta(4, 8);

    ops2[0].input_trace_indices = arena2.alloc_array<crucible::OpIndex>(3);
    ops2[0].input_trace_indices[0] = crucible::OpIndex{}; // null bias
    ops2[0].input_trace_indices[1] = crucible::OpIndex{}; // external x
    ops2[0].input_trace_indices[2] = crucible::OpIndex{}; // external weight

    ops2[0].input_slot_ids = arena2.alloc_array<crucible::SlotId>(3);
    ops2[0].input_slot_ids[0] = crucible::SlotId{}; // null — no slot
    ops2[0].input_slot_ids[1] = crucible::SlotId{0};          // external
    ops2[0].input_slot_ids[2] = crucible::SlotId{1};          // external

    ops2[0].output_slot_ids = arena2.alloc_array<crucible::SlotId>(1);
    ops2[0].output_slot_ids[0] = crucible::SlotId{2};

    auto* slots2 = arena2.alloc_array<crucible::TensorSlot>(3);
    std::memset(slots2, 0, 3 * sizeof(crucible::TensorSlot));
    slots2[0] = {.offset_bytes = 0, .nbytes = 128,
                 .birth_op = OpIndex{0}, .death_op = OpIndex{0},
                 .dtype = ScalarType::Float, .device_type = DeviceType::CPU,
                 .device_idx = 0, .layout = Layout::Strided,
                 .is_external = true, .pad = {}, .slot_id = SlotId{0}, .pad2 = {}};
    slots2[1] = {.offset_bytes = 0, .nbytes = 256,
                 .birth_op = OpIndex{0}, .death_op = OpIndex{0},
                 .dtype = ScalarType::Float, .device_type = DeviceType::CPU,
                 .device_idx = 0, .layout = Layout::Strided,
                 .is_external = true, .pad = {}, .slot_id = SlotId{1}, .pad2 = {}};
    slots2[2] = {.offset_bytes = 0, .nbytes = 128,
                 .birth_op = OpIndex{0}, .death_op = OpIndex{0},
                 .dtype = ScalarType::Float, .device_type = DeviceType::CPU,
                 .device_idx = 0, .layout = Layout::Strided,
                 .is_external = false, .pad = {}, .slot_id = SlotId{2}, .pad2 = {}};

    auto* tg2 = arena2.alloc_obj<crucible::TraceGraph>();
    tg2->ops = ops2;
    tg2->num_ops = 1;
    tg2->slots = slots2;
    tg2->num_slots = 3;
    // No DFG edges (all inputs are external or null).
    crucible::build_csr(arena2, tg2, nullptr, 0, 1);

    crucible::Graph graph2(&pool);
    crucible::lower_trace_to_graph(*tg2, pool, graph2);

    // 2 INPUT nodes (slots 0, 1) + 1 compute node = 3 total.
    // The null input (slot UINT32_MAX) was filtered out.
    assert(graph2.num_nodes() == 3);
    assert(graph2.node(2)->kind == crucible::NodeKind::EXTERN); // GEMM_ADDMM
    assert(graph2.node(2)->num_inputs == 2); // null bias filtered out

    // Input slots on the compute node: only the 2 real inputs.
    assert(graph2.input_slots(NodeId{2}) != nullptr);
    assert(graph2.input_slots(NodeId{2})[0] == SlotId{0});
    assert(graph2.input_slots(NodeId{2})[1] == SlotId{1});

    std::printf("test_lower: all tests passed\n");
    return 0;
}
