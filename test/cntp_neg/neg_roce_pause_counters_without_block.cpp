// The pause counters are files that the fs door opens, and an open can park
// the caller.  The cold init context owns IO but not Block, so it cannot
// read them.

#include <crucible/cntp/RoceConfig.h>
#include <fixy/Ctx.h>

int main() {
    ::fixy::ColdInitCtx init{::foundation::effects::testing::init()};
    auto iface = crucible::cntp::NicInterfaceName::from("eth0").expect("eth0 is an interface name");
    auto counters = crucible::cntp::query_pfc_pause_counters(init, iface);
    return counters.is_ok() ? 0 : 1;
}
