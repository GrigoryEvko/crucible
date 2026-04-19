// Graph IR transform benchmarks.
//
// Measures topological_sort, eliminate_common_subexpressions, and
// compute_fusion_groups on a realistic pointwise-chain graph. Drives
// decisions about how often the scheduler pipeline can afford to run
// these passes per bg drain cycle.
//
// Scales the chain at 3 sizes (64 / 512 / 4096 nodes) so the reader
// sees how each pass scales with graph depth. Same pool/graph reused
// across the non-mutating passes (topo, fusion); CSE rebuilds per
// sample because it mutates (marks dupes DEAD).

#include <cstdint>
#include <cstdio>
#include <vector>

#include <crucible/Effects.h>
#include <crucible/ExprPool.h>
#include <crucible/Graph.h>

#include "bench_harness.h"

using namespace crucible;

static const fx::Bg BG;
static constexpr auto A = BG.alloc;

// Build a pointwise chain of length N: x → op0 → op1 → ... → op(N-1).
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
    bench::print_system_info();
    bench::elevate_priority();

    const bool json = bench::env_json();

    std::printf("=== graph ===\n");

    // Three chain sizes, three Runs each = 9 Reports. Collected into one
    // array so emit_reports can walk everything at once and the JSON tail
    // is a single valid document.
    std::vector<bench::Report> reports;
    reports.reserve(9);

    for (uint32_t n : {64u, 512u, 4096u}) {
        // One long-lived pool + graph drives the non-mutating topo/fusion
        // passes. It outlives all three Runs for this N. The IIFE-lambda
        // pattern from bench_arena captures by reference, so the pool must
        // live in this outer scope.
        ExprPool pool{A};
        Graph    graph{A, &pool};
        build_chain(pool, graph, n);

        char label[64];

        std::snprintf(label, sizeof(label), "topological_sort  (%u nodes)", n);
        reports.push_back(bench::run(label, [&]{
            graph.topological_sort(A);
        }));

        std::snprintf(label, sizeof(label), "compute_fusion    (%u nodes)", n);
        reports.push_back(bench::run(label, [&]{
            uint32_t g = graph.compute_fusion_groups(A);
            bench::do_not_optimize(g);
        }));

        // CSE mutates — rebuild a fresh graph per body call. Auto-batch
        // will replay this N times per timed region; still meaningful
        // because each body is a self-contained build + mutate pair.
        std::snprintf(label, sizeof(label), "cse_build+run     (%u nodes)", n);
        reports.push_back(bench::run(label, [&]{
            ExprPool p2(A);
            Graph    g2(A, &p2);
            build_chain(p2, g2, n);
            uint32_t e = g2.eliminate_common_subexpressions(A);
            bench::do_not_optimize(e);
        }));
    }

    bench::emit_reports(reports, json);
    return 0;
}
