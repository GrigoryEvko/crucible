#include <crucible/TraceGraph.h>
#include <fixy/Core.h>
#include <foundation/effects/Effect.h>

#include "test_assert.h"

#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <vector>

using namespace crucible;

static Edge E(uint32_t src, uint32_t dst, EdgeKind k = EdgeKind::DATA_FLOW) {
    return Edge{.src = OpIndex{src}, .dst = OpIndex{dst}, .src_port = 0, .dst_port = 0, .kind = k, .pad = 0};
}

// The first edge of a run that holds one.
static Edge first_of(::fixy::View<const Edge> edges) { return *edges.get(0).expect("the run holds an edge"); }

// The offset at index of a CSR offset array.
static uint32_t offset_at(::fixy::View<const uint32_t> offsets, uint32_t index) {
    return *offsets.get(index).expect("the index is at most the op count");
}

static void test_empty_graph() {
    auto t = ::foundation::effects::testing::test();
    ::fixy::BumpArena arena{1 << 16};
    TraceGraph g{};
    build_csr(t.alloc, arena, &g, nullptr, 0, 0);
    static_assert(std::is_same_v<TraceGraph::BuiltCount, ::fixy::WriteOnce<uint32_t>>);
    assert(g.num_edges.get_assuming_set() == 0);
    crucible::test::pass("  test_empty:                     PASSED\n");
}

static void test_single_edge_fwd_rev() {
    auto t = ::foundation::effects::testing::test();
    ::fixy::BumpArena arena{1 << 16};
    TraceGraph g{};
    Edge edges[] = {E(0, 1)};
    build_csr(t.alloc, arena, &g, edges, 1, /*num_ops=*/2);
    assert(g.num_edges.get_assuming_set() == 1);
    assert(g.out_degree(OpIndex{0}) == 1);
    assert(g.out_degree(OpIndex{1}) == 0);
    assert(first_of(g.fwd_of(OpIndex{0})).dst == OpIndex{1});
    assert(g.in_degree(OpIndex{0}) == 0);
    assert(g.in_degree(OpIndex{1}) == 1);
    assert(first_of(g.rev_of(OpIndex{1})).src == OpIndex{0});
    crucible::test::pass("  test_single_edge:               PASSED\n");
}

static void test_counting_sort_groups_by_src() {
    // Edges in random src order must all group by src in fwd_edges.
    auto t = ::foundation::effects::testing::test();
    ::fixy::BumpArena arena{1 << 16};
    TraceGraph g{};
    std::vector<Edge> edges = {E(2, 3), E(0, 1), E(2, 4), E(1, 2), E(0, 2), E(2, 5)};
    build_csr(t.alloc, arena, &g, edges.data(), static_cast<uint32_t>(edges.size()), /*num_ops=*/6);

    assert(g.out_degree(OpIndex{0}) == 2);
    assert(g.out_degree(OpIndex{1}) == 1);
    assert(g.out_degree(OpIndex{2}) == 3);
    assert(g.out_degree(OpIndex{3}) == 0);

    // All fwd edges with src=0 must come before any with src=1, etc.
    for (uint32_t op = 0; op < 6; ++op) {
        const OpIndex oi{op};
        for (const Edge& e : g.fwd_of(oi)) {
            assert(e.src == oi);
        }
    }

    // Reverse edges also grouped — every in-edge of op N has dst=N.
    for (uint32_t op = 0; op < 6; ++op) {
        const OpIndex oi{op};
        for (const Edge& e : g.rev_of(oi)) {
            assert(e.dst == oi);
        }
    }
    crucible::test::pass("  test_counting_sort:             PASSED\n");
}

static void test_edge_kind_preserved() {
    auto t = ::foundation::effects::testing::test();
    ::fixy::BumpArena arena{1 << 16};
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
    for (const Edge& e : g.fwd_of(oi0)) {
        if (e.kind == EdgeKind::DATA_FLOW) saw_df = true;
        if (e.kind == EdgeKind::ALIAS) saw_alias = true;
        if (e.kind == EdgeKind::CONTROL_FLOW) saw_cf = true;
    }
    assert(saw_df && saw_alias && saw_cf);
    crucible::test::pass("  test_edge_kind_preserved:       PASSED\n");
}

static void test_offsets_prefix_sum_invariant() {
    auto t = ::foundation::effects::testing::test();
    ::fixy::BumpArena arena{1 << 16};
    TraceGraph g{};
    std::vector<Edge> edges;
    // Chain: 0->1->2->...->9
    for (uint32_t i = 0; i + 1 < 10; ++i)
        edges.push_back(E(i, i + 1));
    build_csr(t.alloc, arena, &g, edges.data(), static_cast<uint32_t>(edges.size()), /*num_ops=*/10);

    assert(g.fwd_offsets.size() == 11 && g.rev_offsets.size() == 11);
    for (uint32_t i = 1; i <= 10; ++i) {
        assert(offset_at(g.fwd_offsets, i) >= offset_at(g.fwd_offsets, i - 1));
        assert(offset_at(g.rev_offsets, i) >= offset_at(g.rev_offsets, i - 1));
    }
    assert(offset_at(g.fwd_offsets, 10) == 9);
    assert(offset_at(g.rev_offsets, 10) == 9);
    crucible::test::pass("  test_prefix_sum:                PASSED\n");
}

static void test_fanout_node_has_multiple_edges() {
    auto t = ::foundation::effects::testing::test();
    ::fixy::BumpArena arena{1 << 16};
    TraceGraph g{};
    // Hub-and-spoke: node 0 → {1, 2, 3, 4, 5, 6, 7, 8, 9}
    std::vector<Edge> edges;
    for (uint32_t i = 1; i < 10; ++i)
        edges.push_back(E(0, i));
    build_csr(t.alloc, arena, &g, edges.data(), 9, /*num_ops=*/10);

    assert(g.out_degree(OpIndex{0}) == 9);
    for (uint32_t i = 1; i < 10; ++i) {
        const OpIndex oi{i};
        assert(g.in_degree(oi) == 1);
        assert(first_of(g.rev_of(oi)).src == OpIndex{0});
    }
    crucible::test::pass("  test_fanout:                    PASSED\n");
}

[[gnu::cold]] int main() {
    test_empty_graph();
    test_single_edge_fwd_rev();
    test_counting_sort_groups_by_src();
    test_edge_kind_preserved();
    test_offsets_prefix_sum_invariant();
    test_fanout_node_has_multiple_edges();
    crucible::test::pass("test_trace_graph: 6 groups, all passed\n");
    return 0;
}
