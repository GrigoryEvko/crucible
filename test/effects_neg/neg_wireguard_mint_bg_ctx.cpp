#include <crucible/cntp/_wip/Wireguard.h>

#include <array>
#include <utility>

int main() {
    auto iface = crucible::cntp::_wip::NicInterfaceName::from("wg0").value();
    auto port = crucible::cntp::_wip::admit_wireguard_port(51820).value();
    auto private_key = crucible::cntp::_wip::admit_wireguard_secret_key_b64(
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=").value();
    auto public_key = crucible::cntp::_wip::admit_wireguard_public_key_b64(
        "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB=").value();
    auto prefix = crucible::cntp::_wip::admit_wireguard_cidr_prefix(24).value();
    std::array<crucible::cntp::_wip::WireguardAllowedIp, 1> allowed{
        crucible::cntp::_wip::WireguardAllowedIp{
            .ipv4_be = 0x0a010000u,
            .prefix_bits = prefix,
        },
    };
    std::array<crucible::cntp::_wip::DeclaredWireguardPeer, 1> peers{
        crucible::cntp::_wip::declare_wireguard_peer(
            public_key,
            crucible::cntp::_wip::WireguardEndpoint{.ipv4_be = 0xc0000201u,
                                              .port = port},
            allowed),
    };
    auto config = crucible::cntp::_wip::mint_wireguard_config(
        iface, port, std::move(private_key), peers).value();
    (void)crucible::cntp::_wip::mint_wireguard_tunnel(
        crucible::effects::BgDrainCtx{}, std::move(config));
}
