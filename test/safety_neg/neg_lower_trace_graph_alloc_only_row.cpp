// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-Lower-6 (#936): allocation authority alone is not enough to run
// lowering.  The function mutates Graph state as background compilation
// work, so CallerRow must contain both Alloc and Bg.
//
// [GCC-WRAPPER-TEXT] - requires-clause constraint failure on
// Subrow<Row<Bg, Alloc>, Row<Alloc>>.

#include <crucible/Lower.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
  auto test = eff::testing::test();
  crucible::ExprPool pool{test.alloc};
  crucible::Graph graph{test.alloc, &pool};
  crucible::TraceGraph trace{};
  using RecordedTraceGraph =
      crucible::LowerTraceGraph<crucible::safety::source::Recorded>;

  (void)crucible::lower_trace_to_graph<eff::Row<eff::Effect::Alloc>>(
      test.alloc, RecordedTraceGraph{&trace}, pool, graph);
  return 0;
}
