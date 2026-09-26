// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// record_measurement takes only a measurement tagged with the Pingmesh
// source.  A raw probe outcome cannot update the latency matrix.

#include <crucible/topology/Pingmesh.h>

int main() {
    auto mesh = crucible::topology::mint_pingmesh<::fixy::ColdInitCtx, 2>(
        ::fixy::ColdInitCtx{::foundation::effects::testing::init()});
    crucible::topology::PingmeshMeasurement raw{};
    (void)mesh.record_measurement(::fixy::BgDrainCtx{::foundation::effects::testing::bg()}, raw);
    return 0;
}
