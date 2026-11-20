// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// TraceRing::drain_pure reads the row of the context of the caller, and the
// gate IsPure asks that the row is empty.  The background thread holds Bg in
// its row, so a context whose row holds Bg fails the gate.  A narrowed
// background context fails the test of the source instead
// (neg_tracering_drain_pure_narrowed_bg.cpp).
//
// Expected diagnostic: IsPure of the row of the context is not satisfied.

#include <crucible/TraceRing.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>

namespace eff = ::foundation::effects;

int main() {
    const ::fixy::BgDrainCtx bg{eff::testing::bg()};
    crucible::TraceRing ring;
    ::fixy::FixedArray<crucible::TraceRing::Entry, 1> out{};
    (void)ring.drain_pure(bg.in_row<eff::Row<eff::Effect::Bg>>(), out.view());
    return 0;
}
