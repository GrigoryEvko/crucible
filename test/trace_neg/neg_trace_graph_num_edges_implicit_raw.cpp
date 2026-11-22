// Negative-compile test.  This file must fail to compile.
//
// TraceGraph build counters are
// WriteOnce<uint32_t>, so consumers must use get() after the build
// phase.  An implicit raw read would hide whether the
// query site has a proof that build_csr already published the counter.
//
// Expected diagnostic: no implicit conversion to uint32_t.

#include <crucible/TraceGraph.h>

int main() {
    crucible::TraceGraph g{};
    g.num_edges.set(0u);
    uint32_t raw = g.num_edges;
    (void)raw;
    return 0;
}
