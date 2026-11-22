// Negative-compile test.  This file must fail to compile.
//
// The worker's constructor is private: mint_congestion_telemetry_worker,
// gated on an Init-row context, is the only way to build one.

#include <crucible/topology/CongestionTelemetryWorker.h>

int main() {
    crucible::topology::CongestionTelemetryWorker<1, 1> worker{};
    (void)worker;
    return 0;
}
