// The hot foreground context owns no effect, so it cannot open the counter
// files that the pause-counter read reaches through the fs door.

#include <crucible/cntp/RoceConfig.h>

int main() {
    auto iface = crucible::cntp::NicInterfaceName::from("eth0").expect("eth0 is an interface name");
    auto counters = crucible::cntp::query_pfc_pause_counters(::foundation::effects::testing::foreground(), iface);
    return counters.is_ok() ? 0 : 1;
}
