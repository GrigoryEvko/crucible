// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Bg names the background context, but effect rows do not imply
// value-level capabilities.  CallerRow must also contain Alloc because
// lower_trace_to_graph allocates Graph scratch through Arena and ExprPool.
//
// [GCC-WRAPPER-TEXT] - requires-clause constraint failure on
// Subrow<Row<Bg, Alloc>, Row<Bg>>.

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

    (void)crucible::lower_trace_to_graph<eff::Row<eff::Effect::Bg>>(
        test.alloc, ::fixy::mint_tagged<::fixy::tags::source::Recorded>(trace_ptr), pool, graph);
    return 0;
}
