// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Minting the congestion telemetry worker needs an execution context.  An
// int in the context slot is refused at the requires-clause, not deep
// inside the factory.

#include <crucible/topology/CongestionTelemetryWorker.h>

int main() {
    auto worker = crucible::topology::mint_congestion_telemetry_worker<1, 1>(0);
    (void)worker;
    return 0;
}
