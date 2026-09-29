// A DSCP is a 6-bit value, so 64 does not satisfy the template clause of
// the config mint.

#include <crucible/cntp/RoceConfig.h>

int main() {
    auto iface = crucible::cntp::NicInterfaceName::from("eth0");
    auto config = crucible::cntp::mint_roce_config<0b00001000, 64>(*iface);
    return config.value().enable_pfc ? 0 : 1;
}
