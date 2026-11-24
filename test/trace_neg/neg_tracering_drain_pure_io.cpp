// Negative-compile test.  This file must fail to compile.
//
// TraceRing::drain_pure reads the row of the context of the caller, and the
// gate IsPure asks that the row is empty.  A context whose row holds IO, such
// as a consumer that writes a checkpoint between drains, fails the gate.
//
// Expected diagnostic: IsPure of the row of the context is not satisfied.

#include <crucible/TraceRing.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>

#include "ring_ends.h"

#include <utility>

namespace eff = ::foundation::effects;

int main() {
    const ::fixy::TestRunnerCtx test{eff::testing::test()};
    ::fixy::FixedArray<crucible::TraceRing::Entry, 1> out{};
    crucible::test::with_ring_consumer([&](crucible::TraceRing::Consumer& ring) noexcept {
        out.with_mut([&](auto run) noexcept -> void {
            (void)crucible::TraceRing::drain_pure(test.in_row<eff::Row<eff::Effect::IO>>(), ring, std::move(run));
        });
    });
    return 0;
}
