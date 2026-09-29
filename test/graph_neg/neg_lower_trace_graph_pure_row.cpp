// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// lower_trace_to_graph<CallerRow> requires
// Subrow<lower_trace_required_row, CallerRow>, where the required row is
// Row<Bg, Alloc>.  Row<> is a foreground/pure caller and cannot lower a
// TraceGraph into a mutable Graph IR.
//
// [GCC-WRAPPER-TEXT] - requires-clause constraint failure on
// Subrow<Row<Bg, Alloc>, Row<>>.

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

    (void)crucible::lower_trace_to_graph<eff::Row<>>(
        test.alloc, ::fixy::mint_tagged<::fixy::tags::source::Recorded>(trace_ptr), pool, graph);
    return 0;
}
