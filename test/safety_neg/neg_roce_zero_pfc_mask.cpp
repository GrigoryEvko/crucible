#include <crucible/cntp/RoceConfig.h>

// GAPS-125 fixture #1: RoCE PFC must select at least one priority.
// Empty masks cannot mint a declared RoCE config.

int main() {
    auto iface = crucible::cntp::NicInterfaceName::from("eth0");
    auto config = crucible::cntp::mint_roce_config<0, 26>(*iface);
    (void)config;
    return 0;
}
