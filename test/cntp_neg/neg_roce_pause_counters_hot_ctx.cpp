// The hot foreground context owns no effect, so it cannot open the counter
// files that the pause-counter read reaches through the fs door.

#include <crucible/cntp/RoceConfig.h>

int main() {
    auto iface = crucible::cntp::NicInterfaceName::from("eth0");
    auto counters = crucible::cntp::query_pfc_pause_counters(::foundation::effects::testing::foreground(), *iface);
    return counters.has_value() ? 0 : 1;
}
