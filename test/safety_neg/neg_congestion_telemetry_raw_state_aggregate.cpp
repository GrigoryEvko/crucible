// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Per-link aggregation takes samples tagged as coming from TCP_INFO.  A
// span of raw CongestionSample values does not convert to a span of
// TcpInfoSnapshot, so counters that no socket reported cannot enter the
// aggregate.

#include <crucible/topology/CongestionTelemetry.h>

#include <array>
#include <span>

int main() {
    crucible::cog::CogIdentity nic{};
    nic.kind = crucible::cog::CogKind::NicPort;
    std::array samples{crucible::topology::CongestionSample{}};
    auto aggregate = crucible::topology::aggregate_congestion(nic, std::span{samples});
    (void)aggregate;
    return 0;
}
