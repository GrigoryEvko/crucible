#include <crucible/cntp/Pacing.h>

// GAPS-121 fixture #1: BBR-compatible pacing configuration admits only
// fq/fq_codel. pfifo_fast can carry packets, but it cannot enforce the
// per-flow pacing BBR depends on.

int main() {
    auto iface = crucible::cntp::NicInterfaceName::from("eth0").value();
    auto config =
        crucible::cntp::mint_bbr_qdisc_config<crucible::cntp::Qdisc::Pfifo>(
            iface);
    (void)config;
    return 0;
}
