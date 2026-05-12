// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-Lower-4 (#934): lower_trace_to_graph propagates the TraceGraph
// source tag to the returned Graph pointer.  Replayed output cannot be
// consumed as Recorded output.
//
// Expected diagnostic: no conversion from LoweredGraph<Replayed> to
// LoweredGraph<Recorded>.

#include <crucible/Lower.h>
#include <crucible/effects/Capabilities.h>

int main() {
  crucible::effects::Test test{};
  crucible::ExprPool pool{test.alloc};
  crucible::Graph graph{test.alloc, &pool};
  crucible::TraceGraph trace{};

  using ReplayedTraceGraph =
      crucible::LowerTraceGraph<crucible::safety::source::Replayed>;
  using RecordedGraph =
      crucible::LoweredGraph<crucible::safety::source::Recorded>;

  RecordedGraph wrong = crucible::lower_trace_to_graph(
      test.alloc, ReplayedTraceGraph{&trace}, pool, graph);
  return wrong.value() == nullptr ? 0 : 1;
}
