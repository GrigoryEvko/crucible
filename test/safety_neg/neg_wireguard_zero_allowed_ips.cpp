#include <crucible/cntp/Wireguard.h>

#include <array>

int main() {
    auto key = crucible::cntp::admit_wireguard_public_key_b64(
        "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB=").value();
    auto port = crucible::cntp::admit_wireguard_port(51820).value();
    std::array<crucible::cntp::WireguardAllowedIp, 0> allowed{};
    (void)crucible::cntp::declare_wireguard_peer(
        key,
        crucible::cntp::WireguardEndpoint{.ipv4_be = 0xc0000201u,
                                          .port = port},
        allowed);
}
