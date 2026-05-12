// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// HS14 fixture for GAPS-134. Foreground hot-path contexts cannot mutate
// pingmesh histograms; admitted measurements are recorded on the Bg row.

#include <crucible/topology/Pingmesh.h>

int main() {
    auto mesh = crucible::topology::mint_pingmesh<
        crucible::effects::ColdInitCtx, 2>(crucible::effects::ColdInitCtx{});
    crucible::topology::DeclaredPingmeshMeasurement m{
        crucible::topology::PingmeshMeasurement{}};
    (void)mesh.record_measurement(crucible::effects::HotFgCtx{}, m);
    return 0;
}
