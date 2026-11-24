#include <crucible/TraceGraph.h>
#include <fixy/Core.h>
#include <foundation/effects/Effect.h>

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>

#include <cstdint>
#include <cstdio>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

using Bytes = ::fixy::Quantity<::fixy::unit::Byte, size_t>;

}  // namespace

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
    TraceScratch scratch{Bytes{size_t{1} << 16}};
    scratch.arena.with_epoch([&](TraceScratch::Epoch& epoch) noexcept {
        TraceGraph const& g = *build_trace_graph(t.alloc, epoch, TraceGraphShape{}, ::fixy::View<const Edge>{});
        CRUCIBLE_FATAL_INVARIANT(g.num_edges == 0 && g.num_ops == 0);
    });
    ::fixy::check::pass(kTest, "  test_empty:                     PASSED\n");
}

static void test_single_edge_fwd_rev() {
    auto t = ::foundation::effects::testing::test();
    TraceScratch scratch{Bytes{size_t{1} << 16}};
    const ::fixy::FixedArray<Edge, 1> edges{E(0, 1)};
    scratch.arena.with_epoch([&](TraceScratch::Epoch& epoch) noexcept {
        TraceGraph const& g = *build_trace_graph(t.alloc, epoch, TraceGraphShape{.num_ops = 2}, edges.view());
        CRUCIBLE_FATAL_INVARIANT(g.num_edges == 1);
        CRUCIBLE_TEST_ASSERT(kTest, g.out_degree(OpIndex{0}) == 1);
        CRUCIBLE_TEST_ASSERT(kTest, g.out_degree(OpIndex{1}) == 0);
        CRUCIBLE_TEST_ASSERT(kTest, first_of(g.fwd_of(OpIndex{0})).dst == OpIndex{1});
        CRUCIBLE_TEST_ASSERT(kTest, g.in_degree(OpIndex{0}) == 0);
        CRUCIBLE_TEST_ASSERT(kTest, g.in_degree(OpIndex{1}) == 1);
        CRUCIBLE_TEST_ASSERT(kTest, first_of(g.rev_of(OpIndex{1})).src == OpIndex{0});
    });
    ::fixy::check::pass(kTest, "  test_single_edge:               PASSED\n");
}

static void test_counting_sort_groups_by_src() {
    // Edges in random src order must all group by src in fwd_edges.
    auto t = ::foundation::effects::testing::test();
    TraceScratch scratch{Bytes{size_t{1} << 16}};
    const ::fixy::FixedArray<Edge, 6> edges{E(2, 3), E(0, 1), E(2, 4), E(1, 2), E(0, 2), E(2, 5)};
    scratch.arena.with_epoch([&](TraceScratch::Epoch& epoch) noexcept {
        TraceGraph const& g = *build_trace_graph(t.alloc, epoch, TraceGraphShape{.num_ops = 6}, edges.view());

        CRUCIBLE_TEST_ASSERT(kTest, g.out_degree(OpIndex{0}) == 2);
        CRUCIBLE_TEST_ASSERT(kTest, g.out_degree(OpIndex{1}) == 1);
        CRUCIBLE_TEST_ASSERT(kTest, g.out_degree(OpIndex{2}) == 3);
        CRUCIBLE_TEST_ASSERT(kTest, g.out_degree(OpIndex{3}) == 0);

        // All fwd edges with src=0 must come before any with src=1, etc.
        for (uint32_t op = 0; op < 6; ++op) {
            const OpIndex oi{op};
            for (const Edge& e : g.fwd_of(oi)) {
                CRUCIBLE_TEST_ASSERT(kTest, e.src == oi);
            }
        }

        // Reverse edges also grouped — every in-edge of op N has dst=N.
        for (uint32_t op = 0; op < 6; ++op) {
            const OpIndex oi{op};
            for (const Edge& e : g.rev_of(oi)) {
                CRUCIBLE_TEST_ASSERT(kTest, e.dst == oi);
            }
        }
    });
    ::fixy::check::pass(kTest, "  test_counting_sort:             PASSED\n");
}

static void test_edge_kind_preserved() {
    auto t = ::foundation::effects::testing::test();
    TraceScratch scratch{Bytes{size_t{1} << 16}};
    const ::fixy::FixedArray<Edge, 3> edges{
        E(0, 1, EdgeKind::DATA_FLOW),
        E(0, 1, EdgeKind::ALIAS),
        E(0, 2, EdgeKind::CONTROL_FLOW),
    };
    scratch.arena.with_epoch([&](TraceScratch::Epoch& epoch) noexcept {
        TraceGraph const& g = *build_trace_graph(t.alloc, epoch, TraceGraphShape{.num_ops = 3}, edges.view());
        CRUCIBLE_TEST_ASSERT(kTest, g.out_degree(OpIndex{0}) == 3);

        bool saw_df = false, saw_alias = false, saw_cf = false;
        const OpIndex oi0{0};
        for (const Edge& e : g.fwd_of(oi0)) {
            if (e.kind == EdgeKind::DATA_FLOW) saw_df = true;
            if (e.kind == EdgeKind::ALIAS) saw_alias = true;
            if (e.kind == EdgeKind::CONTROL_FLOW) saw_cf = true;
        }
        CRUCIBLE_TEST_ASSERT(kTest, saw_df && saw_alias && saw_cf);
    });
    ::fixy::check::pass(kTest, "  test_edge_kind_preserved:       PASSED\n");
}

static void test_offsets_prefix_sum_invariant() {
    auto t = ::foundation::effects::testing::test();
    TraceScratch scratch{Bytes{size_t{1} << 16}};
    ::fixy::FixedArray<Edge, 9> edges{};
    // Chain: 0->1->2->...->9
    for (const auto index : edges.indices()) {
        const auto from = static_cast<uint32_t>(index.raw());
        edges.set(index, E(from, from + 1));
    }
    scratch.arena.with_epoch([&](TraceScratch::Epoch& epoch) noexcept {
        TraceGraph const& g = *build_trace_graph(t.alloc, epoch, TraceGraphShape{.num_ops = 10}, edges.view());

        CRUCIBLE_TEST_ASSERT(kTest, g.fwd_offsets.size() == 11 && g.rev_offsets.size() == 11);
        for (uint32_t i = 1; i <= 10; ++i) {
            CRUCIBLE_TEST_ASSERT(kTest, offset_at(g.fwd_offsets.view(), i) >= offset_at(g.fwd_offsets.view(), i - 1));
            CRUCIBLE_TEST_ASSERT(kTest, offset_at(g.rev_offsets.view(), i) >= offset_at(g.rev_offsets.view(), i - 1));
        }
        CRUCIBLE_TEST_ASSERT(kTest, offset_at(g.fwd_offsets.view(), 10) == 9);
        CRUCIBLE_TEST_ASSERT(kTest, offset_at(g.rev_offsets.view(), 10) == 9);
    });
    ::fixy::check::pass(kTest, "  test_prefix_sum:                PASSED\n");
}

static void test_fanout_node_has_multiple_edges() {
    auto t = ::foundation::effects::testing::test();
    TraceScratch scratch{Bytes{size_t{1} << 16}};
    // Hub-and-spoke: node 0 → {1, 2, 3, 4, 5, 6, 7, 8, 9}
    ::fixy::FixedArray<Edge, 9> edges{};
    for (const auto index : edges.indices()) {
        edges.set(index, E(0, static_cast<uint32_t>(index.raw()) + 1));
    }
    scratch.arena.with_epoch([&](TraceScratch::Epoch& epoch) noexcept {
        TraceGraph const& g = *build_trace_graph(t.alloc, epoch, TraceGraphShape{.num_ops = 10}, edges.view());

        CRUCIBLE_TEST_ASSERT(kTest, g.out_degree(OpIndex{0}) == 9);
        for (uint32_t i = 1; i < 10; ++i) {
            const OpIndex oi{i};
            CRUCIBLE_TEST_ASSERT(kTest, g.in_degree(oi) == 1);
            CRUCIBLE_TEST_ASSERT(kTest, first_of(g.rev_of(oi)).src == OpIndex{0});
        }
    });
    ::fixy::check::pass(kTest, "  test_fanout:                    PASSED\n");
}

[[gnu::cold]] int main() {
    test_empty_graph();
    test_single_edge_fwd_rev();
    test_counting_sort_groups_by_src();
    test_edge_kind_preserved();
    test_offsets_prefix_sum_invariant();
    test_fanout_node_has_multiple_edges();
    ::fixy::check::pass(kTest, "test_trace_graph: 6 groups, all passed\n");
    return 0;
}
