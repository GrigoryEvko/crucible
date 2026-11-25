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

#include "ring_ends.h"

#include <utility>

namespace eff = ::foundation::effects;

int main() {
    const ::fixy::BgDrainCtx bg{eff::testing::bg()};
    auto [out, out_key] = ::fixy::mint_keyed_owner(::fixy::fixed_array_owner<crucible::TraceRing::Entry, 1>);
    crucible::test::with_ring_consumer([&](crucible::TraceRing::Consumer& ring) noexcept {
        out.with_mut(out_key, [&](auto run) noexcept -> void {
            (void)crucible::TraceRing::drain_pure(bg.in_row<eff::Row<>>(), ring, std::move(run));
        });
    });
    return 0;
}
