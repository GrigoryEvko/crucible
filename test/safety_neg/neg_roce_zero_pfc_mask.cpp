// A RoCE config selects at least one PFC priority, so an empty mask does
// not satisfy the template clause of the config mint.

#include <crucible/cntp/RoceConfig.h>

int main() {
    auto iface = crucible::cntp::NicInterfaceName::from("eth0");
    auto config = crucible::cntp::mint_roce_config<0, 26>(*iface);
    return config.value().enable_pfc ? 0 : 1;
}
