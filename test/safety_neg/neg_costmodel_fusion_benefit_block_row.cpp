// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-CostModel-8 fixture: compute_fusion_benefit<CallerRow> is pure
// arithmetic over already-computed costs and requires
// Subrow<CallerRow, Row<>>.  Row<Block> is not pure, preventing blocking
// contexts from being typed as pure projections by accident.
//
// [GCC-WRAPPER-TEXT] - requires-clause constraint failure on
// Subrow<Row<Block>, Row<>>.

#include <crucible/CostModel.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    (void)::crucible::compute_fusion_benefit<
        eff::Row<eff::Effect::Block>>(10.0, 5.0, 64u, 1u);
    return 0;
}
