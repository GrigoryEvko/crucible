// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The snapshot mint takes a TCP sample under the TcpInfo source tag.  A
// raw congestion sample does not convert to it, so a sample reaches the
// mint only from the socket harvest or a source that names itself.

#include <crucible/topology/Telemetry.h>

namespace topology = crucible::topology;

[[maybe_unused]] static int mint_from_raw_sample(crucible::cog::CogIdentity const& nic,
                                                 topology::CongestionSample const& raw) {
    auto snapshot = topology::mint_nic_telemetry_snapshot(
        nic, 1, topology::declare_netdev_counters(topology::NetdevCounters{}),
        topology::declare_qdisc_backlog(topology::QdiscBacklog{}),
        topology::declare_sysctl_snapshot(topology::SysctlSnapshot{}), raw,
        topology::declare_nic_thermal_sample(topology::NicThermalSample{}), 1);
    return snapshot.has_value() ? 0 : 1;
}

int main() { return 0; }
