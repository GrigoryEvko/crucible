// Negative-compile test.  This file must fail to compile.
//
// TraceRing::try_append_pure reads the row of the context of the caller, and
// the gate IsPure asks that the row is empty.  The context of the load stage
// of the background thread holds the widest row that a background context
// can hold: Bg, Alloc, IO, Block and Nondet.  It fails the gate.
//
// Expected diagnostic: IsPure of the row of the context is not satisfied.

#include <crucible/TraceRing.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>

#include "ring_ends.h"

namespace eff = ::foundation::effects;

int main() {
    const ::fixy::BgLoadCtx load{eff::testing::bg()};
    crucible::TraceRing::Entry e{};
    crucible::test::with_ring_producer([&](crucible::TraceRing::Producer& ring) noexcept {
        (void)crucible::TraceRing::try_append_pure(load, ring, e);
    });
    return 0;
}
