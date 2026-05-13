#include <crucible/cntp/_wip/Wireguard.h>

#include <array>

int main() {
    auto key = crucible::cntp::_wip::admit_wireguard_public_key_b64(
        "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB=").value();
    auto port = crucible::cntp::_wip::admit_wireguard_port(51820).value();
    std::array<crucible::cntp::_wip::WireguardAllowedIp, 0> allowed{};
    (void)crucible::cntp::_wip::declare_wireguard_peer(
        key,
        crucible::cntp::_wip::WireguardEndpoint{.ipv4_be = 0xc0000201u,
                                          .port = port},
        allowed);
}
