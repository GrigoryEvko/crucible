// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// lower_trace_to_graph requires the input TraceGraph to carry Recorded or
// Replayed provenance.  A raw TraceGraph* must not cross the lowering
// boundary.
//
// Expected diagnostic: no matching lower_trace_to_graph overload.

#include <crucible/Lower.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

namespace eff = ::foundation::effects;

int main() {
    auto test = eff::testing::test();
    crucible::ExprPool pool{test.alloc};
    crucible::Graph graph{test.alloc, &pool};
    crucible::TraceGraph trace{};
    using LowerBgAllocRow = eff::Row<eff::Effect::Bg, eff::Effect::Alloc>;
    (void)crucible::lower_trace_to_graph<LowerBgAllocRow>(test.alloc, &trace, pool, graph);
    return 0;
}
