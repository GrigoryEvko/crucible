#include <crucible/cntp/RoceConfig.h>

// GAPS-125 fixture #2: RoCE DSCP is a 6-bit value. Values above 63
// cannot mint a declared RoCE config.

int main() {
    auto iface = crucible::cntp::NicInterfaceName::from("eth0");
    auto config = crucible::cntp::mint_roce_config<0b0000'1000, 64>(*iface);
    (void)config;
    return 0;
}
