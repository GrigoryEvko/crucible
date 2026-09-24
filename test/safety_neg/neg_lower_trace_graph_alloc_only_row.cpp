// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Allocation authority alone is not enough to run lowering.  The function
// mutates Graph state as background compilation work, so CallerRow must
// contain both Alloc and Bg.
//
// [GCC-WRAPPER-TEXT] - requires-clause constraint failure on
// Subrow<Row<Bg, Alloc>, Row<Alloc>>.

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

    (void)crucible::lower_trace_to_graph<eff::Row<eff::Effect::Alloc>>(
        test.alloc, ::fixy::mint_tagged<::fixy::tags::source::Recorded>(trace_ptr), pool, graph);
    return 0;
}
