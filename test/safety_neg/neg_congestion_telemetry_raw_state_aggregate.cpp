#include <crucible/topology/CongestionTelemetry.h>

#include <array>
#include <span>

// Per-link aggregation accepts TcpInfoSnapshot values tagged with
// source::TcpInfo, not raw CongestionSample counters.

int main() {
    crucible::cog::CogIdentity nic{};
    nic.kind = crucible::cog::CogKind::NicPort;
    std::array samples{crucible::topology::CongestionSample{}};
    auto aggregate = crucible::topology::aggregate_congestion(nic, std::span{samples});
    (void)aggregate;
    return 0;
}
