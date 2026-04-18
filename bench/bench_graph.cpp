// Graph IR transform benchmarks.
//
// Measures: topological_sort, eliminate_common_subexpressions,
// compute_fusion_groups on a realistic chain-plus-fanout graph.
// Targets inform whether the scheduler pipeline can keep up with
// multi-iteration background work.

#include <crucible/Effects.h>
#include <crucible/ExprPool.h>
#include <crucible/Graph.h>

#include <cstdint>
#include <cstdio>
#include <vector>

#include "bench_harness.h"

using namespace crucible;

static const fx::Bg BG;
static constexpr auto A = BG.alloc;

// Build a pointwise chain of length N: x → op0 → op1 → ... → opN-1.
// Each op has one input (previous op's output) and one output.
static void build_chain(ExprPool& pool, Graph& graph, uint32_t n) {
    const Expr* size = pool.integer(A, 128);
    const Expr* ranges[1] = {size};

    GraphNode* prev = graph.add_input(
        A, ScalarType::Float, /*device_idx=*/0, ranges);

    for (uint32_t i = 0; i < n; i++) {
        GraphNode* deps[] = {prev};
        prev = graph.add_pointwise(
            A, ranges, ScalarType::Float, 0, nullptr, deps);
    }

    NodeId out[] = {prev->id};
    graph.set_graph_outputs(A, out);
}

int main() {
    std::printf("bench_graph: transform pipeline\n");

    for (uint32_t n : {64u, 512u, 4096u}) {
        std::printf("\n── %u-node chain ──\n", n);

        // Build once for topo+CSE+fusion benches — those are non-mutating
        // enough that we can re-run them on the same graph.
        ExprPool pool{A};
        Graph graph{A, &pool};
        build_chain(pool, graph, n);

        const uint64_t iter_topo = 200'000u / n;
        const uint64_t iter_fuse = 100'000u / n;
        const uint64_t iter_cse  = 100'000u / n;

        char label[64];
        std::snprintf(label, sizeof(label), "  topological_sort  (%u nodes)", n);
        BENCH(label, iter_topo, {
            graph.topological_sort(A);
        });

        std::snprintf(label, sizeof(label), "  compute_fusion    (%u nodes)", n);
        BENCH(label, iter_fuse, {
            uint32_t g = graph.compute_fusion_groups(A);
            bench::DoNotOptimize(g);
        });

        // CSE mutates the graph (marks dupes DEAD).  Rebuild for each iter.
        auto cse_once = [&] {
            ExprPool p2(A);
            Graph g2(A, &p2);
            build_chain(p2, g2, n);
            uint32_t e = g2.eliminate_common_subexpressions(A);
            bench::DoNotOptimize(e);
        };
        std::snprintf(label, sizeof(label), "  cse_build+run     (%u nodes)", n);
        BENCH(label, iter_cse, { cse_once(); });
    }

    return 0;
}
