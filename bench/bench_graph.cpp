// Graph IR transform benchmarks.
//
// Every Run builds a fresh ExprPool + Graph + pointwise chain inside
// the body, then runs one transform pass. Rebuild-per-body is load-
// bearing: the transforms (topological_sort, compute_fusion_groups,
// eliminate_common_subexpressions) each allocate scratch arrays into
// Graph::arena_, which is a bump allocator with no per-call free.
// Running them N times on the SAME graph would accumulate N copies of
// that scratch in the arena — unbounded RSS under the default 100k
// samples × 10k warmup. Rebuild-per-body keeps memory bounded to one
// chain + one transform's scratch at any moment.
//
// Side effect: the numbers below are "build_chain + transform", not
// pure transform cost. Subtract the build_chain baseline at the same
// N to isolate the transform. For scheduler budgeting, the combined
// number is what matters — transforms always run against a freshly-
// recorded graph.

#include <cstdint>
#include <cstdio>
#include <vector>

#include <crucible/effects/Capabilities.h>
#include <crucible/ExprPool.h>
#include <crucible/Graph.h>

#include "bench_harness.h"

using namespace crucible;

static const effects::Bg BG;
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

    std::printf("=== graph ===\n\n");

    // 4 Reports per size × 3 sizes = 12. First Report per size is the
    // build_chain baseline; subtract from the others to isolate the
    // transform cost. The bench harness caps each Run's wall time
    // (default 10s) so heavy bodies don't accumulate RSS via glibc's
    // heap-pool growth under 100k-sample iteration.
    std::vector<bench::Report> reports;

    for (uint32_t n : {64u, 512u, 4096u}) {
        char label[64];

        std::snprintf(label, sizeof(label), "build_chain                (%u nodes)", n);
        reports.push_back(bench::run(label, [n]{
            ExprPool pool(A);
            Graph    graph(A, &pool);
            build_chain(pool, graph, n);
            bench::do_not_optimize(graph);
        }));

        std::snprintf(label, sizeof(label), "build + topological_sort   (%u nodes)", n);
        reports.push_back(bench::run(label, [n]{
            ExprPool pool(A);
            Graph    graph(A, &pool);
            build_chain(pool, graph, n);
            graph.topological_sort(A);
            bench::do_not_optimize(graph);
        }));

        std::snprintf(label, sizeof(label), "build + compute_fusion     (%u nodes)", n);
        reports.push_back(bench::run(label, [n]{
            ExprPool pool(A);
            Graph    graph(A, &pool);
            build_chain(pool, graph, n);
            uint32_t g = graph.compute_fusion_groups(A);
            bench::do_not_optimize(g);
        }));

        std::snprintf(label, sizeof(label), "build + cse                (%u nodes)", n);
        reports.push_back(bench::run(label, [n]{
            ExprPool pool(A);
            Graph    graph(A, &pool);
            build_chain(pool, graph, n);
            uint32_t e = graph.eliminate_common_subexpressions(A);
            bench::do_not_optimize(e);
        }));
    }

    bench::emit_reports_text(reports);

    // Transform deltas at N=4096 (indices 8/9/10/11 = build/topo/fusion/cse).
    std::printf("\n=== compare — pure transform vs build baseline (N=4096) ===\n");
    std::printf("  topo   = Δ over build_chain:\n  ");
    bench::compare(reports[8], reports[9]).print_text(stdout);
    std::printf("  fusion = Δ over build_chain:\n  ");
    bench::compare(reports[8], reports[10]).print_text(stdout);
    std::printf("  cse    = Δ over build_chain:\n  ");
    bench::compare(reports[8], reports[11]).print_text(stdout);

    bench::emit_reports_json(reports, json);
    return 0;
}
