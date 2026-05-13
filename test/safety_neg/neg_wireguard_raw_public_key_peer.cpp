#include <crucible/cntp/Wireguard.h>

#include <array>

int main() {
    crucible::cntp::WireguardKeyB64 raw_key{};
    auto port = crucible::cntp::admit_wireguard_port(51820).value();
    auto prefix = crucible::cntp::admit_wireguard_cidr_prefix(24).value();
    std::array<crucible::cntp::WireguardAllowedIp, 1> allowed{
        crucible::cntp::WireguardAllowedIp{
            .ipv4_be = 0x0a010000u,
            .prefix_bits = prefix,
        },
    };
    (void)crucible::cntp::declare_wireguard_peer(
        raw_key,
        crucible::cntp::WireguardEndpoint{.ipv4_be = 0xc0000201u,
                                          .port = port},
        allowed);
}
