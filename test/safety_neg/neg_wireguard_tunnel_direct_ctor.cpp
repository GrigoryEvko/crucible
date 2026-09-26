// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// A tunnel is built only by mint_wireguard_tunnel, which checks the context
// and the config.  The constructor takes a key that only the mint can make,
// and an empty brace list does not make one.

#include <crucible/cntp/_wip/Wireguard.h>

#include <utility>

namespace wg = crucible::cntp::_wip;

int build_tunnel(wg::DeclaredWireguardConfig config);

int build_tunnel(wg::DeclaredWireguardConfig config) {
    const wg::WireguardTunnel<2> tunnel{{}, std::move(config)};
    return tunnel.peer_count();
}

int main() { return 0; }
