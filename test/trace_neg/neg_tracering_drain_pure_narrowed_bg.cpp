// Negative-compile test.  This file must fail to compile.
//
// TraceRing::drain_pure promises a pure caller.  The background thread
// narrows its context to the empty row with in_row, and the narrowed context
// keeps the background source, which permits Bg, Alloc, IO, Block, Nondet and
// Div.  The gate reads the source as well as the row, so the narrowed context
// fails the gate.
//
// Expected diagnostic: CtxHasPureSource of the narrowed context is not
// satisfied.

#include <crucible/TraceRing.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>

namespace eff = ::foundation::effects;

int main() {
    const ::fixy::BgDrainCtx bg{eff::testing::bg()};
    crucible::TraceRing ring;
    ::fixy::FixedArray<crucible::TraceRing::Entry, 1> out{};
    (void)ring.drain_pure(bg.in_row<eff::Row<>>(), out.view());
    return 0;
}
