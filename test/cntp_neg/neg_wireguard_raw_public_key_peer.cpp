// NEGATIVE-COMPILE TEST.  This file must fail to compile.
//
// A peer takes a declared public key, and admit_wireguard_public_key_b64 is
// the one function that makes one.  Raw key characters do not convert.

#include <crucible/cntp/_wip/Wireguard.h>

#include <array>

int main() {
    namespace wg = crucible::cntp::_wip;
    const wg::WireguardKeyChars raw_key{};
    auto port = wg::admit_wireguard_port(51820).value();
    std::array<wg::WireguardAllowedIp, 1> allowed{};
    auto peer =
        wg::declare_wireguard_peer(raw_key, wg::WireguardEndpoint{.ipv4_be = 0xc0000201u, .port = port}, allowed);
    return peer.value().allowed_ip_count;
}
