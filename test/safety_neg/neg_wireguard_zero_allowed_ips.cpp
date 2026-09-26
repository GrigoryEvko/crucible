// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// A WireGuard peer owns at least one allowed network.  An empty set of
// allowed networks does not satisfy the shape of declare_wireguard_peer.

#include <crucible/cntp/_wip/Wireguard.h>

#include <array>

int main() {
    namespace wg = crucible::cntp::_wip;
    auto key = wg::admit_wireguard_public_key_b64("BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB=").value();
    auto port = wg::admit_wireguard_port(51820).value();
    std::array<wg::WireguardAllowedIp, 0> allowed{};
    auto peer = wg::declare_wireguard_peer(key, wg::WireguardEndpoint{.ipv4_be = 0xc0000201u, .port = port}, allowed);
    return peer.value().allowed_ip_count;
}
