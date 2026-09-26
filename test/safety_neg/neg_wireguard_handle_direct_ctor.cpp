// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// A tunnel handle comes only from the plan of a tunnel.  A handle built by
// hand does not compile.

#include <crucible/cntp/_wip/Wireguard.h>

int main() {
    namespace wg = crucible::cntp::_wip;
    auto iface = wg::NicInterfaceName::from("wg0").value();
    auto key = wg::admit_wireguard_public_key_b64("BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB=").value();
    const wg::WireguardTunnelHandle handle{iface, key, 1u};
    return static_cast<int>(handle.generation());
}
