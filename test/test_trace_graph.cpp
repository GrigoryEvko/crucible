// Tests for build_csr — the counting-sort CSR builder used by
// BackgroundThread::build_trace and by the Sugiyama layout pass.

#include <crucible/Arena.h>
#include <crucible/Effects.h>
#include <crucible/TraceGraph.h>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace crucible;

static Edge E(uint32_t src, uint32_t dst, EdgeKind k = EdgeKind::DATA_FLOW) {
    return Edge{.src = OpIndex{src}, .dst = OpIndex{dst},
                .src_port = 0, .dst_port = 0, .kind = k, .pad = 0};
}

static void test_empty_graph() {
    fx::Test t;
    Arena arena{1 << 16};
    TraceGraph g{};
    build_csr(t.alloc, arena, &g, nullptr, 0, 0);
    assert(g.num_edges == 0);
    std::printf("  test_empty:                     PASSED\n");
}

static void test_single_edge_fwd_rev() {
    fx::Test t;
    Arena arena{1 << 16};
    TraceGraph g{};
    Edge edges[] = {E(0, 1)};
    build_csr(t.alloc, arena, &g, edges, 1, /*num_ops=*/2);
    assert(g.num_edges == 1);
    // fwd: op 0 has 1 out-edge.
    assert(g.out_degree(OpIndex{0}) == 1);
    assert(g.out_degree(OpIndex{1}) == 0);
    assert(g.fwd_begin(OpIndex{0})->dst == OpIndex{1});
    // rev: op 1 has 1 in-edge.
    assert(g.in_degree(OpIndex{0}) == 0);
    assert(g.in_degree(OpIndex{1}) == 1);
    assert(g.rev_begin(OpIndex{1})->src == OpIndex{0});
    std::printf("  test_single_edge:               PASSED\n");
}

static void test_counting_sort_groups_by_src() {
    // Edges in random src order must all group by src in fwd_edges.
    fx::Test t;
    Arena arena{1 << 16};
    TraceGraph g{};
    std::vector<Edge> edges = {
        E(2, 3), E(0, 1), E(2, 4), E(1, 2), E(0, 2), E(2, 5)
    };
    build_csr(t.alloc, arena, &g, edges.data(),
              static_cast<uint32_t>(edges.size()), /*num_ops=*/6);

    // Degrees.
    assert(g.out_degree(OpIndex{0}) == 2);
    assert(g.out_degree(OpIndex{1}) == 1);
    assert(g.out_degree(OpIndex{2}) == 3);
    assert(g.out_degree(OpIndex{3}) == 0);

    // All fwd edges with src=0 must come before any with src=1, etc.
    for (uint32_t op = 0; op < 6; ++op) {
        const OpIndex oi{op};
        for (const Edge* e = g.fwd_begin(oi); e != g.fwd_end(oi); ++e) {
            assert(e->src == oi);
        }
    }

    // Reverse edges also grouped — every in-edge of op N has dst=N.
    for (uint32_t op = 0; op < 6; ++op) {
        const OpIndex oi{op};
        for (const Edge* e = g.rev_begin(oi); e != g.rev_end(oi); ++e) {
            assert(e->dst == oi);
        }
    }
    std::printf("  test_counting_sort:             PASSED\n");
}

static void test_edge_kind_preserved() {
    fx::Test t;
    Arena arena{1 << 16};
    TraceGraph g{};
    Edge edges[] = {
        E(0, 1, EdgeKind::DATA_FLOW),
        E(0, 1, EdgeKind::ALIAS),
        E(0, 2, EdgeKind::CONTROL_FLOW),
    };
    build_csr(t.alloc, arena, &g, edges, 3, /*num_ops=*/3);
    assert(g.out_degree(OpIndex{0}) == 3);

    bool saw_df = false, saw_alias = false, saw_cf = false;
    const OpIndex oi0{0};
    for (const Edge* e = g.fwd_begin(oi0); e != g.fwd_end(oi0); ++e) {
        if (e->kind == EdgeKind::DATA_FLOW)     saw_df = true;
        if (e->kind == EdgeKind::ALIAS)         saw_alias = true;
        if (e->kind == EdgeKind::CONTROL_FLOW)  saw_cf = true;
    }
    assert(saw_df && saw_alias && saw_cf);
    std::printf("  test_edge_kind_preserved:       PASSED\n");
}

static void test_offsets_prefix_sum_invariant() {
    fx::Test t;
    Arena arena{1 << 16};
    TraceGraph g{};
    std::vector<Edge> edges;
    // Chain: 0->1->2->...->9
    for (uint32_t i = 0; i + 1 < 10; ++i) edges.push_back(E(i, i + 1));
    build_csr(t.alloc, arena, &g, edges.data(),
              static_cast<uint32_t>(edges.size()), /*num_ops=*/10);

    // Offsets monotonic non-decreasing.
    for (uint32_t i = 0; i <= 10; ++i) {
        if (i > 0) {
            assert(g.fwd_offsets[i] >= g.fwd_offsets[i - 1]);
            assert(g.rev_offsets[i] >= g.rev_offsets[i - 1]);
        }
    }
    // Last offset equals num_edges.
    assert(g.fwd_offsets[10] == 9);
    assert(g.rev_offsets[10] == 9);
    std::printf("  test_prefix_sum:                PASSED\n");
}

static void test_fanout_node_has_multiple_edges() {
    fx::Test t;
    Arena arena{1 << 16};
    TraceGraph g{};
    // Hub-and-spoke: node 0 → {1, 2, 3, 4, 5, 6, 7, 8, 9}
    std::vector<Edge> edges;
    for (uint32_t i = 1; i < 10; ++i) edges.push_back(E(0, i));
    build_csr(t.alloc, arena, &g, edges.data(), 9, /*num_ops=*/10);

    assert(g.out_degree(OpIndex{0}) == 9);
    for (uint32_t i = 1; i < 10; ++i) {
        const OpIndex oi{i};
        assert(g.in_degree(oi) == 1);
        assert(g.rev_begin(oi)->src == OpIndex{0});
    }
    std::printf("  test_fanout:                    PASSED\n");
}

int main() {
    test_empty_graph();
    test_single_edge_fwd_rev();
    test_counting_sort_groups_by_src();
    test_edge_kind_preserved();
    test_offsets_prefix_sum_invariant();
    test_fanout_node_has_multiple_edges();
    std::printf("test_trace_graph: 6 groups, all passed\n");
    return 0;
}
