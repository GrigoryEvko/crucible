// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// HS14 fixture for GAPS-134. Raw probe outcomes cannot update the
// topology latency matrix; callers must pass source::Pingmesh-tagged
// measurements admitted by the probe boundary.

#include <crucible/topology/Pingmesh.h>

int main() {
    auto mesh = crucible::topology::mint_pingmesh<
        crucible::effects::ColdInitCtx, 2>(crucible::effects::ColdInitCtx{});
    crucible::topology::PingmeshMeasurement raw{};
    (void)mesh.record_measurement(crucible::effects::BgDrainCtx{}, raw);
    return 0;
}
