// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// lower_trace_to_graph propagates the TraceGraph source tag to the
// returned Graph pointer.  Replayed output cannot be consumed as Recorded
// output.
//
// Expected diagnostic: no conversion from LoweredGraph<Replayed> to
// LoweredGraph<Recorded>.

#include <crucible/Lower.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

namespace eff = ::foundation::effects;

int main() {
    auto test = eff::testing::test();
    crucible::ExprPool pool{test.alloc};
    crucible::Graph graph{test.alloc, &pool};
    crucible::TraceGraph trace{};
    const crucible::TraceGraph* const trace_ptr = &trace;

    using RecordedGraph = crucible::LoweredGraph<::fixy::tags::source::Recorded>;
    using LowerBgAllocRow = eff::Row<eff::Effect::Bg, eff::Effect::Alloc>;

    RecordedGraph wrong = crucible::lower_trace_to_graph<LowerBgAllocRow>(
        test.alloc, ::fixy::mint_tagged<::fixy::tags::source::Replayed>(trace_ptr), pool, graph);
    return wrong.value() == nullptr ? 0 : 1;
}
