// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// TraceRing::try_append_pure reads the row of the context of the caller, and
// the gate IsPure asks that the row is empty.  A context whose row holds
// Alloc fails the gate, because the pure append allocates nothing.
//
// Expected diagnostic: IsPure of the row of the context is not satisfied.

#include <crucible/TraceRing.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>

namespace eff = ::foundation::effects;

int main() {
    const ::fixy::TestRunnerCtx test{eff::testing::test()};
    crucible::TraceRing ring;
    crucible::TraceRing::Entry e{};
    (void)ring.try_append_pure(test.in_row<eff::Row<eff::Effect::Alloc>>(), e);
    return 0;
}
