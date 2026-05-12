#include <crucible/cntp/RoceConfig.h>

// GAPS-125 fixture #3: apply_roce_config requires a DeclaredRoceConfig
// tagged with source::RoceConfig, not a raw config struct.

int main() {
    auto iface = crucible::cntp::NicInterfaceName::from("eth0");
    crucible::cntp::RoceConfig raw{.interface = *iface};
    auto result = crucible::cntp::apply_roce_config(raw);
    (void)result;
    return 0;
}
