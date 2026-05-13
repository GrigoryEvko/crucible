// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-TraceGraph-3 (#1044): TraceGraph build counters are
// TraceGraph::BuiltCount = safety::WriteOnce<uint32_t>.  `build_csr`
// publishes num_ops exactly once; raw assignment would bypass the
// single-publication lifecycle gate.
//
// Expected diagnostic: no assignment operator from raw uint32_t.

#include <crucible/TraceGraph.h>

int main() {
  crucible::TraceGraph g{};
  g.num_ops = 1u;
  return 0;
}
