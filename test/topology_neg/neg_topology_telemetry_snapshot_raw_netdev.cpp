// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The snapshot mint takes netdev counters under the kernel telemetry
// source tag.  Raw counters do not convert to them.

#include <crucible/topology/Telemetry.h>

namespace topology = crucible::topology;

[[maybe_unused]] static int mint_from_raw_counters(crucible::cog::CogIdentity const& nic,
                                                   topology::TcpInfoSnapshot const& tcp) {
    auto snapshot = topology::mint_nic_telemetry_snapshot(
        nic, 1, topology::NetdevCounters{}, topology::declare_qdisc_backlog(topology::QdiscBacklog{}),
        topology::declare_sysctl_snapshot(topology::SysctlSnapshot{}), tcp,
        topology::declare_nic_thermal_sample(topology::NicThermalSample{}), 1);
    return snapshot.has_value() ? 0 : 1;
}

int main() { return 0; }
