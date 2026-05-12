// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-CostModel-8 fixture: evaluate_cost<CallerRow> is a pure
// projection.  Row<Bg> marks background-thread authority and is not a
// subrow of Row<>, so the requires-clause rejects the call before the
// evaluator can be composed into a non-pure effect row.
//
// [GCC-WRAPPER-TEXT] - requires-clause constraint failure on
// Subrow<Row<Bg>, Row<>>.

#include <crucible/CostModel.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    auto hw = ::crucible::blackwell_b200();
    ::crucible::KernelConfig cfg{};
    (void)::crucible::evaluate_cost<eff::Row<eff::Effect::Bg>>(
        1u, 1u, 1u, ::crucible::ScalarType::Float, cfg, hw);
    return 0;
}
