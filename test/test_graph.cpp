#include <crucible/Graph.h>
#include <crucible/effects/_Capabilities.h>
#include <crucible/ExprPool.h>
#include <crucible/Types.h>
#include "test_assert.h"
#include <cstdio>
#include <span>

[[gnu::cold]] int main() {
    auto test = crucible::effects::testing::test();
    crucible::ExprPool pool(test.alloc);
    crucible::Graph graph(test.alloc, &pool);

    const crucible::Expr* low_cached = pool.integer(test.alloc, crucible::ExprPool::kIntCacheLow);
    const crucible::Expr* high_cached = pool.integer(test.alloc, crucible::ExprPool::kIntCacheHigh);
    const crucible::Expr* outside_cached = pool.integer(test.alloc, crucible::ExprPool::kIntCacheHigh + 1);
    assert(low_cached == pool.integer(test.alloc, crucible::ExprPool::kIntCacheLow));
    assert(high_cached == pool.integer(test.alloc, crucible::ExprPool::kIntCacheHigh));
    assert(outside_cached == pool.integer(test.alloc, crucible::ExprPool::kIntCacheHigh + 1));
    assert(low_cached->as_int() == crucible::ExprPool::kIntCacheLow);
    assert(high_cached->as_int() == crucible::ExprPool::kIntCacheHigh);
    assert(outside_cached->as_int() == crucible::ExprPool::kIntCacheHigh + 1);

    assert(crucible::classify_node_kind(crucible::CKernelId::ACT_RELU) == crucible::NodeKind::POINTWISE);
    assert(crucible::classify_node_kind(crucible::CKernelId::ACT_GELU) == crucible::NodeKind::POINTWISE);
    assert(crucible::classify_node_kind(crucible::CKernelId::ACT_SOFTMAX) == crucible::NodeKind::POINTWISE);
    assert(crucible::classify_node_kind(crucible::CKernelId::ACT_MISH) == crucible::NodeKind::POINTWISE);
    assert(crucible::classify_node_kind(crucible::CKernelId::EWISE_ADD) == crucible::NodeKind::POINTWISE);
    assert(crucible::classify_node_kind(crucible::CKernelId::EWISE_WHERE) == crucible::NodeKind::POINTWISE);
    assert(crucible::classify_node_kind(crucible::CKernelId::EWISE_EXP) == crucible::NodeKind::POINTWISE);
    assert(crucible::classify_node_kind(crucible::CKernelId::EWISE_FILL) == crucible::NodeKind::POINTWISE);

    assert(crucible::classify_node_kind(crucible::CKernelId::REDUCE_SUM) == crucible::NodeKind::REDUCTION);
    assert(crucible::classify_node_kind(crucible::CKernelId::REDUCE_TOPK) == crucible::NodeKind::REDUCTION);
    assert(crucible::classify_node_kind(crucible::CKernelId::REDUCE_MEAN) == crucible::NodeKind::REDUCTION);

    assert(crucible::classify_node_kind(crucible::CKernelId::REDUCE_CUMSUM) == crucible::NodeKind::SCAN);
    assert(crucible::classify_node_kind(crucible::CKernelId::ASSOC_SCAN) == crucible::NodeKind::SCAN);

    assert(crucible::classify_node_kind(crucible::CKernelId::VIEW) == crucible::NodeKind::NOP);
    assert(crucible::classify_node_kind(crucible::CKernelId::RESHAPE) == crucible::NodeKind::NOP);
    assert(crucible::classify_node_kind(crucible::CKernelId::PERMUTE) == crucible::NodeKind::NOP);
    assert(crucible::classify_node_kind(crucible::CKernelId::UNFOLD) == crucible::NodeKind::NOP);
    assert(crucible::classify_node_kind(crucible::CKernelId::CAT) == crucible::NodeKind::NOP);

    assert(crucible::classify_node_kind(crucible::CKernelId::COPY_) == crucible::NodeKind::MUTATION);

    assert(crucible::classify_node_kind(crucible::CKernelId::OPAQUE) == crucible::NodeKind::EXTERN);
    assert(crucible::classify_node_kind(crucible::CKernelId::GEMM_MM) == crucible::NodeKind::EXTERN);
    assert(crucible::classify_node_kind(crucible::CKernelId::SDPA) == crucible::NodeKind::EXTERN);
    assert(crucible::classify_node_kind(crucible::CKernelId::LAYER_NORM) == crucible::NodeKind::EXTERN);
    assert(crucible::classify_node_kind(crucible::CKernelId::CONV2D) == crucible::NodeKind::EXTERN);
    assert(crucible::classify_node_kind(crucible::CKernelId::POOL_MAX2D) == crucible::NodeKind::EXTERN);
    assert(crucible::classify_node_kind(crucible::CKernelId::COMM_ALLREDUCE) == crucible::NodeKind::EXTERN);
    assert(crucible::classify_node_kind(crucible::CKernelId::SELECTIVE_SCAN) == crucible::NodeKind::EXTERN);

    const crucible::Expr* s0 = pool.integer(test.alloc, 32);
    const crucible::Expr* s1 = pool.integer(test.alloc, 64);
    const crucible::Expr* sizes[2] = {s0, s1};

    auto* inp0 = graph.add_input(test.alloc, crucible::ScalarType::Float, 0, std::span{sizes, 2u});
    assert(inp0->id == crucible::NodeId{0});

    auto* inp1 = graph.add_input(test.alloc, crucible::ScalarType::Float, 0, std::span{sizes, 2u});
    assert(inp1->id == crucible::NodeId{1});

    crucible::GraphNode* add_inputs[2] = {inp0, inp1};
    auto* add_node = graph.add_pointwise(test.alloc, std::span{sizes, 2u}, crucible::ScalarType::Float, 0, nullptr,
                                         std::span{add_inputs, 2u});
    assert(add_node->id == crucible::NodeId{2});
    assert(add_node->num_inputs == 2);

    // An input node reads no slot, because it is itself the source.
    crucible::SlotId inp0_out_slots[] = {crucible::SlotId{0}};
    crucible::SlotId inp1_out_slots[] = {crucible::SlotId{1}};
    graph.set_output_slots(test.alloc, crucible::NodeId{0}, inp0_out_slots);
    graph.set_output_slots(test.alloc, crucible::NodeId{1}, inp1_out_slots);

    crucible::SlotId add_in_slots[] = {crucible::SlotId{0}, crucible::SlotId{1}};
    crucible::SlotId add_out_slots[] = {crucible::SlotId{2}};
    graph.set_input_slots(test.alloc, crucible::NodeId{2}, add_in_slots);
    graph.set_output_slots(test.alloc, crucible::NodeId{2}, add_out_slots);

    // A slot table that was never set reads back as null rather than
    // as an empty table.
    assert(graph.input_slots(crucible::NodeId{0}) == nullptr);
    assert(graph.input_slots(crucible::NodeId{1}) == nullptr);

    assert(graph.output_slots(crucible::NodeId{0}) != nullptr);
    assert(graph.output_slots(crucible::NodeId{0})[0] == crucible::SlotId{0});
    assert(graph.output_slots(crucible::NodeId{1}) != nullptr);
    assert(graph.output_slots(crucible::NodeId{1})[0] == crucible::SlotId{1});

    assert(graph.input_slots(crucible::NodeId{2}) != nullptr);
    assert(graph.input_slots(crucible::NodeId{2})[0] == crucible::SlotId{0});
    assert(graph.input_slots(crucible::NodeId{2})[1] == crucible::SlotId{1});
    assert(graph.output_slots(crucible::NodeId{2}) != nullptr);
    assert(graph.output_slots(crucible::NodeId{2})[0] == crucible::SlotId{2});

    static_assert(sizeof(crucible::GraphNode) == 64, "GraphNode must remain 64B (one cache line)");

    // Nothing consumes the added node, so only its being named as a
    // graph output keeps it alive through the sweep.
    crucible::NodeId out_ids[] = {crucible::NodeId{2}};
    graph.set_graph_outputs(test.alloc, out_ids);
    graph.eliminate_dead_nodes();

    assert(graph.input_slots(crucible::NodeId{2})[0] == crucible::SlotId{0});
    assert(graph.input_slots(crucible::NodeId{2})[1] == crucible::SlotId{1});
    assert(graph.output_slots(crucible::NodeId{2})[0] == crucible::SlotId{2});

    // Sorting assigns a schedule order and leaves the slot tables
    // alone.
    graph.topological_sort(test.alloc);
    assert(graph.input_slots(crucible::NodeId{2})[0] == crucible::SlotId{0});
    assert(graph.output_slots(crucible::NodeId{0})[0] == crucible::SlotId{0});

    {
        crucible::ExprPool cse_pool(test.alloc);
        crucible::Graph g(test.alloc, &cse_pool);

        const crucible::Expr* r0 = cse_pool.integer(test.alloc, 32);
        const crucible::Expr* r1 = cse_pool.integer(test.alloc, 64);
        const crucible::Expr* ranges[2] = {r0, r1};

        auto* x = g.add_input(test.alloc, crucible::ScalarType::Float, 0, std::span{ranges, 2u});
        auto* y = g.add_input(test.alloc, crucible::ScalarType::Float, 0, std::span{ranges, 2u});

        auto make_add_body = [&]() {
            auto* body = g.alloc_body(test.alloc, 4);
            body->ops[0] = {.op = crucible::MicroOp::LOAD, .dtype = crucible::ScalarType::Float};
            body->ops[1] = {.op = crucible::MicroOp::LOAD, .dtype = crucible::ScalarType::Float};
            body->ops[2] = {.op = crucible::MicroOp::ADD, .dtype = crucible::ScalarType::Float, .operands = {0, 1, 0}};
            body->ops[3] = {
                .op = crucible::MicroOp::STORE, .dtype = crucible::ScalarType::Float, .operands = {2, 0, 0}};
            body->num_loads = 2;
            body->store_op = 3;
            return body;
        };

        // Same inputs and same body, so the two nodes compute the same
        // value and one of them is redundant.
        crucible::GraphNode* pw_in[2] = {x, y};
        auto* pw1 = g.add_pointwise(test.alloc, std::span{ranges, 2u}, crucible::ScalarType::Float, 0, make_add_body(),
                                    std::span{pw_in, 2u});
        auto* pw2 = g.add_pointwise(test.alloc, std::span{ranges, 2u}, crucible::ScalarType::Float, 0, make_add_body(),
                                    std::span{pw_in, 2u});

        crucible::GraphNode* sum_in[2] = {pw1, pw2};
        auto* sum_node = g.add_pointwise(test.alloc, std::span{ranges, 2u}, crucible::ScalarType::Float, 0,
                                         make_add_body(), std::span{sum_in, 2u});

        crucible::NodeId cse_outs[] = {sum_node->id};
        g.set_graph_outputs(test.alloc, cse_outs);

        assert(g.num_nodes() == 5);  // two inputs, two pointwise, one sum
        assert(g.count_live() == 5);

        uint32_t elim = g.eliminate_common_subexpressions(test.alloc);
        assert(elim == 1);  // the second pointwise node gives way to the first

        assert(sum_node->inputs[0] == pw1);
        assert(sum_node->inputs[1] == pw1);
        assert(pw2->is_dead());

        // The surviving node now serves both operands of the sum.
        assert(pw1->num_uses == 2);

        // The same body over different inputs computes a different
        // value, so neither node is redundant.
        crucible::Graph g2(test.alloc, &cse_pool);
        auto* a = g2.add_input(test.alloc, crucible::ScalarType::Float, 0, std::span{ranges, 2u});
        auto* b = g2.add_input(test.alloc, crucible::ScalarType::Float, 0, std::span{ranges, 2u});
        auto* c = g2.add_input(test.alloc, crucible::ScalarType::Float, 0, std::span{ranges, 2u});
        crucible::GraphNode* ab_in[2] = {a, b};
        crucible::GraphNode* ac_in[2] = {a, c};
        auto* ab = g2.add_pointwise(test.alloc, std::span{ranges, 2u}, crucible::ScalarType::Float, 0, make_add_body(),
                                    std::span{ab_in, 2u});
        auto* ac = g2.add_pointwise(test.alloc, std::span{ranges, 2u}, crucible::ScalarType::Float, 0, make_add_body(),
                                    std::span{ac_in, 2u});
        crucible::GraphNode* final_in[2] = {ab, ac};
        auto* final_node = g2.add_pointwise(test.alloc, std::span{ranges, 2u}, crucible::ScalarType::Float, 0,
                                            make_add_body(), std::span{final_in, 2u});
        crucible::NodeId g2_outs[] = {final_node->id};
        g2.set_graph_outputs(test.alloc, g2_outs);

        uint32_t elim2 = g2.eliminate_common_subexpressions(test.alloc);
        assert(elim2 == 0);
        assert(!ab->is_dead());
        assert(!ac->is_dead());
    }

    std::printf("test_graph: all tests passed\n");
    return 0;
}
