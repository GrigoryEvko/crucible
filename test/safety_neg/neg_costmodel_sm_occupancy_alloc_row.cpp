// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-CostModel-8 fixture: sm_occupancy<CallerRow> is a pure
// projection and requires CallerRow to satisfy Subrow<CallerRow, Row<>>.
// Row<Alloc> is not pure, so a caller that may allocate cannot silently
// invoke the pure cost-model projection.
//
// [GCC-WRAPPER-TEXT] - requires-clause constraint failure on
// Subrow<Row<Alloc>, Row<>>.

#include <crucible/CostModel.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    auto hw = ::crucible::blackwell_b200();
    (void)::crucible::sm_occupancy<eff::Row<eff::Effect::Alloc>>(
        ::crucible::ValidRegsPerThread{std::uint16_t{32}}, 0u, 8u, hw);
    return 0;
}
