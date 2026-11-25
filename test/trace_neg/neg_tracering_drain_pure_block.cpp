// Negative-compile test.  This file must fail to compile.
//
// TraceRing::drain_pure reads the row of the context of the caller, and the
// gate IsPure asks that the row is empty.  A context whose row holds Block,
// such as a consumer that waits in the kernel between drains, fails the
// gate.
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
    auto [out, out_key] = ::fixy::mint_keyed_owner(::fixy::fixed_array_owner<crucible::TraceRing::Entry, 1>);
    crucible::test::with_ring_consumer([&](crucible::TraceRing::Consumer& ring) noexcept {
        out.with_mut(out_key, [&](auto run) noexcept -> void {
            (void)crucible::TraceRing::drain_pure(test.in_row<eff::Row<eff::Effect::Block>>(), ring, std::move(run));
        });
    });
    return 0;
}
