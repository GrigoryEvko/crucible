// Negative-compile test.  This file must fail to compile.
//
// build_trace_graph gives a graph its counts when it makes the graph, and
// each count of TraceGraph is const.  So no code writes a count after the
// build, and an assignment to num_ops does not compile.
//
// Expected diagnostic: an assignment of the read-only member num_ops.

#include <crucible/TraceGraph.h>

int main() {
    crucible::TraceGraph graph{};
    graph.num_ops = 1u;
    return 0;
}
