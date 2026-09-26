// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// A WireGuard config has at least one peer.  An empty set of peers does not
// satisfy the shape of mint_wireguard_config.

#include <crucible/cntp/_wip/Wireguard.h>

#include <array>
#include <utility>

int main() {
    namespace wg = crucible::cntp::_wip;
    auto iface = wg::NicInterfaceName::from("wg0").value();
    auto port = wg::admit_wireguard_port(51820).value();
    auto private_key = wg::admit_wireguard_secret_key_b64("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=").value();
    std::array<wg::DeclaredWireguardPeer, 0> peers{};
    auto config = wg::mint_wireguard_config(iface, port, std::move(private_key), peers);
    return config.has_value() ? 0 : 1;
}
