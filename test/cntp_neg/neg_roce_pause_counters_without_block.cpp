// The pause counters are files that the fs door opens, and an open can park
// the caller.  The cold init context owns IO but not Block, so it cannot
// read them.

#include <crucible/cntp/RoceConfig.h>
#include <fixy/Ctx.h>

int main() {
    ::fixy::ColdInitCtx init{::foundation::effects::testing::init()};
    auto iface = crucible::cntp::NicInterfaceName::from("eth0");
    auto counters = crucible::cntp::query_pfc_pause_counters(init, *iface);
    return counters.has_value() ? 0 : 1;
}
