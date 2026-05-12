// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-CostModel-8 fixture: wave_efficiency<CallerRow> is a pure
// projection and requires CallerRow to satisfy Subrow<CallerRow, Row<>>.
// Row<IO> contains an effect atom, so {IO} is not a subrow of the empty
// pure row.  The cost model must not be callable from an IO-bearing
// context without an explicit effect-boundary decision.
//
// [GCC-WRAPPER-TEXT] - requires-clause constraint failure on
// Subrow<Row<IO>, Row<>>.

#include <crucible/CostModel.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    auto hw = ::crucible::blackwell_b200();
    (void)::crucible::wave_efficiency<eff::Row<eff::Effect::IO>>(1u, hw);
    return 0;
}
