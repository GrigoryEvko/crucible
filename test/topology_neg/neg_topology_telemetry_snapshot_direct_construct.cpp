// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A snapshot is reached only through its mint, which checks the cog and
// computes the effective bandwidth.  Its empty-slot constructor is
// private, so a caller cannot build a snapshot that skipped both.

#include <crucible/topology/Telemetry.h>

int main() {
    crucible::topology::NicTelemetrySnapshot snapshot;
    return static_cast<int>(snapshot.sequence());
}
