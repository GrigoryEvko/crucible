// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Only an Init-row context mints the congestion telemetry worker.  A
// background context may record into a worker but may not create one.

#include <crucible/topology/CongestionTelemetryWorker.h>

int main() {
    ::fixy::BgDrainCtx bg{::foundation::effects::testing::bg()};
    auto worker = crucible::topology::mint_congestion_telemetry_worker<1, 1>(bg);
    (void)worker;
    return 0;
}
