// Negative-compile test.  This file must fail to compile.
//
// TraceRing::drain_pure reads the row of the context of the caller, and the
// gate IsPure asks that the row is empty.  A context whose row holds Alloc,
// such as a consumer that allocates between drains, fails the gate.
//
// Expected diagnostic: IsPure of the row of the context is not satisfied.

#include <crucible/TraceRing.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>

#include <utility>

namespace eff = ::foundation::effects;

int main() {
    const ::fixy::TestRunnerCtx test{eff::testing::test()};
    crucible::TraceRing ring;
    ::fixy::FixedArray<crucible::TraceRing::Entry, 1> out{};
    out.with_mut([&](auto run) noexcept -> void {
        (void)ring.drain_pure(test.in_row<eff::Row<eff::Effect::Alloc>>(), std::move(run));
    });
    return 0;
}
