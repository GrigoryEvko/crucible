#include <crucible/topology/CongestionTelemetry.h>

#include <array>
#include <span>

// GAPS-123 fixture #2: per-link aggregation accepts TcpInfoSnapshot
// values tagged with source::TcpInfo, not raw CongestionState counters.

int main() {
    crucible::cog::CogIdentity nic{};
    nic.kind = crucible::cog::CogKind::NicPort;
    std::array samples{crucible::topology::CongestionState{}};
    auto aggregate = crucible::topology::aggregate_congestion(
        nic, std::span{samples});
    (void)aggregate;
    return 0;
}
