// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-TraceGraph-3 (#1044): TraceGraph build counters are
// WriteOnce<uint32_t>, so consumers must use get()/get_assuming_set()
// after the build phase.  An implicit raw read would hide whether the
// query site has a proof that build_csr already published the counter.
//
// Expected diagnostic: no implicit conversion to uint32_t.

#include <crucible/ir001/TraceGraph.h>

int main() {
  crucible::TraceGraph g{};
  g.num_edges.set(0u);
  uint32_t raw = g.num_edges;
  (void)raw;
  return 0;
}
