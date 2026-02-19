#include <crucible/Graph.h>
#include <crucible/ExprPool.h>
#include <crucible/Types.h>
#include <cassert>
#include <cstdio>
#include <span>

int main() {
    crucible::ExprPool pool;
    crucible::Graph graph(&pool);

    // ── classify_node_kind coverage ─────────────────────────────────

    // Pointwise: activations + elementwise binary/unary
    assert(crucible::classify_node_kind(crucible::CKernelId::ACT_RELU)
           == crucible::NodeKind::POINTWISE);
    assert(crucible::classify_node_kind(crucible::CKernelId::ACT_GELU)
           == crucible::NodeKind::POINTWISE);
    assert(crucible::classify_node_kind(crucible::CKernelId::ACT_SOFTMAX)
           == crucible::NodeKind::POINTWISE);
    assert(crucible::classify_node_kind(crucible::CKernelId::ACT_MISH)
           == crucible::NodeKind::POINTWISE);
    assert(crucible::classify_node_kind(crucible::CKernelId::EWISE_ADD)
           == crucible::NodeKind::POINTWISE);
    assert(crucible::classify_node_kind(crucible::CKernelId::EWISE_WHERE)
           == crucible::NodeKind::POINTWISE);
    assert(crucible::classify_node_kind(crucible::CKernelId::EWISE_EXP)
           == crucible::NodeKind::POINTWISE);
    assert(crucible::classify_node_kind(crucible::CKernelId::EWISE_FILL)
           == crucible::NodeKind::POINTWISE);

    // Reductions
    assert(crucible::classify_node_kind(crucible::CKernelId::REDUCE_SUM)
           == crucible::NodeKind::REDUCTION);
    assert(crucible::classify_node_kind(crucible::CKernelId::REDUCE_TOPK)
           == crucible::NodeKind::REDUCTION);
    assert(crucible::classify_node_kind(crucible::CKernelId::REDUCE_MEAN)
           == crucible::NodeKind::REDUCTION);

    // Scan (cumsum + associative scan)
    assert(crucible::classify_node_kind(crucible::CKernelId::REDUCE_CUMSUM)
           == crucible::NodeKind::SCAN);
    assert(crucible::classify_node_kind(crucible::CKernelId::ASSOC_SCAN)
           == crucible::NodeKind::SCAN);

    // Data movement → NOP
    assert(crucible::classify_node_kind(crucible::CKernelId::VIEW)
           == crucible::NodeKind::NOP);
    assert(crucible::classify_node_kind(crucible::CKernelId::RESHAPE)
           == crucible::NodeKind::NOP);
    assert(crucible::classify_node_kind(crucible::CKernelId::PERMUTE)
           == crucible::NodeKind::NOP);
    assert(crucible::classify_node_kind(crucible::CKernelId::UNFOLD)
           == crucible::NodeKind::NOP);
    assert(crucible::classify_node_kind(crucible::CKernelId::CAT)
           == crucible::NodeKind::NOP);

    // Mutation
    assert(crucible::classify_node_kind(crucible::CKernelId::COPY_)
           == crucible::NodeKind::MUTATION);

    // EXTERN: GEMM, conv, attention, normalization, pooling, etc.
    assert(crucible::classify_node_kind(crucible::CKernelId::OPAQUE)
           == crucible::NodeKind::EXTERN);
    assert(crucible::classify_node_kind(crucible::CKernelId::GEMM_MM)
           == crucible::NodeKind::EXTERN);
    assert(crucible::classify_node_kind(crucible::CKernelId::SDPA)
           == crucible::NodeKind::EXTERN);
    assert(crucible::classify_node_kind(crucible::CKernelId::LAYER_NORM)
           == crucible::NodeKind::EXTERN);
    assert(crucible::classify_node_kind(crucible::CKernelId::CONV2D)
           == crucible::NodeKind::EXTERN);
    assert(crucible::classify_node_kind(crucible::CKernelId::POOL_MAX2D)
           == crucible::NodeKind::EXTERN);
    assert(crucible::classify_node_kind(crucible::CKernelId::COMM_ALLREDUCE)
           == crucible::NodeKind::EXTERN);
    assert(crucible::classify_node_kind(crucible::CKernelId::SELECTIVE_SCAN)
           == crucible::NodeKind::EXTERN);

    // ── Graph construction with slot ID side-tables ─────────────────

    // Create symbolic sizes: [32, 64]
    const crucible::Expr* s0 = pool.integer(32);
    const crucible::Expr* s1 = pool.integer(64);
    const crucible::Expr* sizes[2] = {s0, s1};

    // Node 0: INPUT (external weight tensor, slot 0)
    auto* inp0 = graph.add_input(
        crucible::ScalarType::Float, 0, std::span{sizes, 2u});
    assert(inp0->id == crucible::NodeId{0});

    // Node 1: INPUT (external activation tensor, slot 1)
    auto* inp1 = graph.add_input(
        crucible::ScalarType::Float, 0, std::span{sizes, 2u});
    assert(inp1->id == crucible::NodeId{1});

    // Node 2: POINTWISE add(inp0, inp1) — output slot 2, reads from slots 0 and 1
    crucible::GraphNode* add_inputs[2] = {inp0, inp1};
    auto* add_node = graph.add_pointwise(
        std::span{sizes, 2u},
        crucible::ScalarType::Float, 0,
        nullptr,
        std::span{add_inputs, 2u});
    assert(add_node->id == crucible::NodeId{2});
    assert(add_node->num_inputs == 2);

    // ── Set slot IDs ────────────────────────────────────────────────

    // Input nodes: no input slots (they ARE the inputs), output slot is their own
    crucible::SlotId inp0_out_slots[] = {crucible::SlotId{0}};
    crucible::SlotId inp1_out_slots[] = {crucible::SlotId{1}};
    graph.set_output_slots(crucible::NodeId{0}, inp0_out_slots);
    graph.set_output_slots(crucible::NodeId{1}, inp1_out_slots);

    // Add node: reads from slots 0 and 1, writes to slot 2
    crucible::SlotId add_in_slots[] = {crucible::SlotId{0}, crucible::SlotId{1}};
    crucible::SlotId add_out_slots[] = {crucible::SlotId{2}};
    graph.set_input_slots(crucible::NodeId{2}, add_in_slots);
    graph.set_output_slots(crucible::NodeId{2}, add_out_slots);

    // ── Verify slot IDs survive ─────────────────────────────────────

    // Input nodes have no input slots (never set → nullptr)
    assert(graph.input_slots(crucible::NodeId{0}) == nullptr);
    assert(graph.input_slots(crucible::NodeId{1}) == nullptr);

    // Input nodes have output slots
    assert(graph.output_slots(crucible::NodeId{0}) != nullptr);
    assert(graph.output_slots(crucible::NodeId{0})[0] == crucible::SlotId{0});
    assert(graph.output_slots(crucible::NodeId{1}) != nullptr);
    assert(graph.output_slots(crucible::NodeId{1})[0] == crucible::SlotId{1});

    // Add node has both input and output slots
    assert(graph.input_slots(crucible::NodeId{2}) != nullptr);
    assert(graph.input_slots(crucible::NodeId{2})[0] == crucible::SlotId{0});
    assert(graph.input_slots(crucible::NodeId{2})[1] == crucible::SlotId{1});
    assert(graph.output_slots(crucible::NodeId{2}) != nullptr);
    assert(graph.output_slots(crucible::NodeId{2})[0] == crucible::SlotId{2});

    // ── Verify GraphNode is still exactly 64 bytes ──────────────────
    static_assert(sizeof(crucible::GraphNode) == 64,
                  "GraphNode must remain 64B (one cache line)");

    // ── Verify Graph transforms don't disturb slot tables ───────────

    // DCE should not affect slot tables (add_node has uses from nothing,
    // but graph outputs keep it alive)
    crucible::NodeId out_ids[] = {crucible::NodeId{2}};
    graph.set_graph_outputs(out_ids);
    graph.eliminate_dead_nodes();

    // Slots survive DCE
    assert(graph.input_slots(crucible::NodeId{2})[0] == crucible::SlotId{0});
    assert(graph.input_slots(crucible::NodeId{2})[1] == crucible::SlotId{1});
    assert(graph.output_slots(crucible::NodeId{2})[0] == crucible::SlotId{2});

    // Topo sort sets schedule_order but doesn't touch slots
    graph.topological_sort();
    assert(graph.input_slots(crucible::NodeId{2})[0] == crucible::SlotId{0});
    assert(graph.output_slots(crucible::NodeId{0})[0] == crucible::SlotId{0});

    std::printf("test_graph: all tests passed\n");
    return 0;
}
