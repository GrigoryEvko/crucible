// Negative-compile test.  This file must fail to compile.
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

#include "ring_ends.h"

#include <utility>

namespace eff = ::foundation::effects;

int main() {
    const ::fixy::BgDrainCtx bg{eff::testing::bg()};
    auto [out, out_key] = ::fixy::mint_keyed_owner(::fixy::fixed_array_owner<crucible::TraceRing::Entry, 1>);
    crucible::test::with_ring_consumer([&](crucible::TraceRing::Consumer& ring) noexcept {
        out.with_mut(out_key, [&](auto run) noexcept -> void {
            (void)crucible::TraceRing::drain_pure(bg.in_row<eff::Row<eff::Effect::Bg>>(), ring, std::move(run));
        });
    });
    return 0;
}
