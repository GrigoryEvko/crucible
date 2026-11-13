// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Per-link aggregation takes samples tagged as coming from TCP_INFO.  A
// view of raw CongestionSample values does not convert to a view of
// TcpInfoSnapshot, so counters that no socket reported cannot enter the
// aggregate.

#include <crucible/topology/CongestionTelemetry.h>

int main() {
    crucible::cog::CogIdentity nic{};
    nic.kind = crucible::cog::CogKind::NicPort;
    ::foundation::core::FixedArray<crucible::topology::CongestionSample, 1> const samples{};
    auto aggregate = crucible::topology::aggregate_congestion(nic, samples.view());
    (void)aggregate;
    return 0;
}
