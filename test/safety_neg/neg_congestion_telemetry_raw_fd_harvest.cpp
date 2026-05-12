#include <crucible/topology/CongestionTelemetry.h>

// GAPS-123 fixture #1: live TCP_INFO harvest requires an admitted
// SocketFd. Raw int descriptors cannot cross the telemetry boundary.

int main() {
    auto sample = crucible::topology::harvest_socket(3);
    (void)sample;
    return 0;
}
