// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-Lower-6 (#936): Bg names the background context, but effect rows
// do not imply value-level capabilities.  CallerRow must also contain
// Alloc because lower_trace_to_graph allocates Graph scratch through
// Arena and ExprPool.
//
// [GCC-WRAPPER-TEXT] - requires-clause constraint failure on
// Subrow<Row<Bg, Alloc>, Row<Bg>>.

#include <crucible/Lower.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
  eff::Test test{};
  crucible::ExprPool pool{test.alloc};
  crucible::Graph graph{test.alloc, &pool};
  crucible::TraceGraph trace{};
  using RecordedTraceGraph =
      crucible::LowerTraceGraph<crucible::safety::source::Recorded>;

  (void)crucible::lower_trace_to_graph<eff::Row<eff::Effect::Bg>>(
      test.alloc, RecordedTraceGraph{&trace}, pool, graph);
  return 0;
}
