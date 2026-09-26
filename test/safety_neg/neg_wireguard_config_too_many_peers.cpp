// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// A WireGuard config holds at most kWireguardMaxPeers peers.  A set of 17
// peers does not satisfy the shape of mint_wireguard_config.

#include <crucible/cntp/_wip/Wireguard.h>

#include <array>
#include <utility>

namespace wg = crucible::cntp::_wip;

int mint_too_many(std::array<wg::DeclaredWireguardPeer, 17> const& peers);

int mint_too_many(std::array<wg::DeclaredWireguardPeer, 17> const& peers) {
    auto iface = wg::NicInterfaceName::from("wg0").value();
    auto port = wg::admit_wireguard_port(51820).value();
    auto private_key = wg::admit_wireguard_secret_key_b64("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=").value();
    auto config = wg::mint_wireguard_config(iface, port, std::move(private_key), peers);
    return config.has_value() ? 0 : 1;
}

int main() { return 0; }
