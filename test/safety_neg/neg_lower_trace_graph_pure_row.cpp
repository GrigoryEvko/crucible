// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-Lower-6 (#936): lower_trace_to_graph<CallerRow> requires
// Subrow<lower_trace_required_row, CallerRow>, where the required row is
// Row<Bg, Alloc>.  Row<> is a foreground/pure caller and cannot lower a
// TraceGraph into a mutable Graph IR.
//
// [GCC-WRAPPER-TEXT] - requires-clause constraint failure on
// Subrow<Row<Bg, Alloc>, Row<>>.

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

  (void)crucible::lower_trace_to_graph<eff::Row<>>(
      test.alloc, RecordedTraceGraph{&trace}, pool, graph);
  return 0;
}
