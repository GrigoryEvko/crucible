// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A telemetry history is reached only through its mint, which checks
// the context.  Its default constructor is private.

#include <crucible/topology/Telemetry.h>

int main() {
    crucible::topology::NicTelemetryHistory<4> history;
    return history.count(::fixy::ColdInitCtx{::foundation::effects::testing::init()});
}
