#include <crucible/cntp/_wip/Wireguard.h>

#include <array>

int main() {
    crucible::cntp::_wip::WireguardKeyB64 raw_key{};
    auto port = crucible::cntp::_wip::admit_wireguard_port(51820).value();
    auto prefix = crucible::cntp::_wip::admit_wireguard_cidr_prefix(24).value();
    std::array<crucible::cntp::_wip::WireguardAllowedIp, 1> allowed{
        crucible::cntp::_wip::WireguardAllowedIp{
            .ipv4_be = 0x0a010000u,
            .prefix_bits = prefix,
        },
    };
    (void)crucible::cntp::_wip::declare_wireguard_peer(
        raw_key,
        crucible::cntp::_wip::WireguardEndpoint{.ipv4_be = 0xc0000201u,
                                          .port = port},
        allowed);
}
