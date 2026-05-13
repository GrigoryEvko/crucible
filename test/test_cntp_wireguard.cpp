#include <crucible/cntp/Wireguard.h>

#include <array>
#include <cassert>
#include <concepts>
#include <cstdio>
#include <string_view>
#include <type_traits>
#include <utility>

namespace cntp = crucible::cntp;
namespace eff = crucible::effects;
namespace saf = crucible::safety;

namespace {

constexpr std::string_view kPrivateKey =
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";
constexpr std::string_view kPeerA =
    "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB=";
constexpr std::string_view kPeerB =
    "CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC=";
constexpr std::string_view kPsk =
    "DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD=";

[[nodiscard]] cntp::NicInterfaceName iface() {
    auto admitted = cntp::NicInterfaceName::from("wg0");
    assert(admitted.has_value());
    return *admitted;
}

[[nodiscard]] cntp::WireguardPort port(std::uint16_t value = 51820) {
    auto admitted = cntp::admit_wireguard_port(value);
    assert(admitted.has_value());
    return *admitted;
}

[[nodiscard]] cntp::WireguardAllowedIp cidr(std::uint32_t ipv4_be,
                                            std::uint8_t prefix) {
    auto admitted = cntp::admit_wireguard_cidr_prefix(prefix);
    assert(admitted.has_value());
    return cntp::WireguardAllowedIp{
        .ipv4_be = ipv4_be,
        .prefix_bits = *admitted,
    };
}

[[nodiscard]] cntp::DeclaredWireguardPublicKey pub(std::string_view key) {
    auto admitted = cntp::admit_wireguard_public_key_b64(key);
    assert(admitted.has_value());
    return *admitted;
}

[[nodiscard]] cntp::WireguardSecretKey secret(std::string_view key) {
    auto admitted = cntp::admit_wireguard_secret_key_b64(key);
    assert(admitted.has_value());
    return std::move(*admitted);
}

[[nodiscard]] cntp::DeclaredWireguardPeer peer_a() {
    std::array<cntp::WireguardAllowedIp, 1> allowed{
        cidr(0x0a010000u, 24),
    };
    return cntp::declare_wireguard_peer(
        pub(kPeerA),
        cntp::WireguardEndpoint{.ipv4_be = 0xc0000201u, .port = port(51820)},
        allowed,
        true);
}

[[nodiscard]] cntp::DeclaredWireguardPeer peer_b() {
    std::array<cntp::WireguardAllowedIp, 2> allowed{
        cidr(0x0a020000u, 24),
        cidr(0x0a030000u, 24),
    };
    return cntp::declare_wireguard_peer(
        pub(kPeerB),
        cntp::WireguardEndpoint{.ipv4_be = 0xc0000202u, .port = port(51821)},
        allowed);
}

[[nodiscard]] cntp::DeclaredWireguardConfig config_one_peer() {
    std::array<cntp::DeclaredWireguardPeer, 1> peers{peer_a()};
    auto config = cntp::mint_wireguard_config(
        iface(), port(), secret(kPrivateKey), peers);
    assert(config.has_value());
    return std::move(*config);
}

void test_admission_and_names() {
    assert(cntp::wireguard_error_name(
               cntp::WireguardError::BackendUnavailable)
           == std::string_view{"BackendUnavailable"});
    assert(cntp::wireguard_error_name(cntp::WireguardError::DuplicatePeer)
           == std::string_view{"DuplicatePeer"});
    assert(cntp::wireguard_error_name(cntp::WireguardError::InvalidEndpoint)
           == std::string_view{"InvalidEndpoint"});

    assert(cntp::admit_wireguard_public_key_b64(kPeerA).has_value());
    assert(cntp::admit_wireguard_secret_key_b64(kPsk).has_value());
    assert(!cntp::admit_wireguard_public_key_b64("").has_value());
    assert(!cntp::admit_wireguard_public_key_b64("bad").has_value());
    assert(!cntp::admit_wireguard_public_key_b64(
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA@=").has_value());
    assert(!cntp::admit_wireguard_public_key_b64(
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA").has_value());
    assert(!cntp::admit_wireguard_public_key_b64(
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA==").has_value());
    assert(cntp::admit_wireguard_port(1).has_value());
    assert(!cntp::admit_wireguard_port(0).has_value());
    assert(cntp::admit_wireguard_cidr_prefix(32).has_value());
    assert(!cntp::admit_wireguard_cidr_prefix(33).has_value());

    std::printf("  test_admission_and_names: PASSED\n");
}

void test_config_and_backend_boundary() {
    auto config = config_one_peer();
    assert(config.value().peer_count == 1);
    assert(config.value().private_key.size() == kPrivateKey.size());
    assert(!config.value().has_preshared_key);
    assert(validate_wireguard_config(config).has_value());

    auto backend = cntp::bring_up_wireguard(config);
    assert(!backend.has_value());
    assert(backend.error() == cntp::WireguardError::BackendUnavailable);

    auto add = cntp::apply_wireguard_peer_add(config, peer_b());
    assert(!add.has_value());
    assert(add.error() == cntp::WireguardError::BackendUnavailable);

    auto remove = cntp::apply_wireguard_peer_remove(config, pub(kPeerA));
    assert(!remove.has_value());
    assert(remove.error() == cntp::WireguardError::BackendUnavailable);

    auto not_found = cntp::apply_wireguard_peer_remove(config, pub(kPeerB));
    assert(!not_found.has_value());
    assert(not_found.error() == cntp::WireguardError::PeerNotFound);

    std::printf("  test_config_and_backend_boundary: PASSED\n");
}

void test_preshared_key_and_endpoint_validation() {
    std::array<cntp::DeclaredWireguardPeer, 1> peers{peer_a()};
    auto with_psk = cntp::mint_wireguard_config_with_psk(
        iface(), port(), secret(kPrivateKey), secret(kPsk), peers);
    assert(with_psk.has_value());
    assert(with_psk->value().has_preshared_key);
    assert(with_psk->value().preshared_key.size() == kPsk.size());
    assert(validate_wireguard_config(*with_psk).has_value());

    auto empty_psk = cntp::mint_wireguard_config_with_psk(
        iface(), port(), secret(kPrivateKey),
        cntp::empty_wireguard_secret_key(), peers);
    assert(!empty_psk.has_value());
    assert(empty_psk.error() == cntp::WireguardError::EmptyKey);

    auto endpoint = cntp::validate_wireguard_endpoint(
        cntp::WireguardEndpoint{.ipv4_be = 0u, .port = port(51820)});
    assert(!endpoint.has_value());
    assert(endpoint.error() == cntp::WireguardError::InvalidEndpoint);

    std::printf("  test_preshared_key_and_endpoint_validation: PASSED\n");
}

void test_tunnel_plan_mutation() {
    eff::ColdInitCtx init{};
    auto tunnel = cntp::mint_wireguard_tunnel<2>(init, config_one_peer());
    assert(tunnel.has_value());
    assert(tunnel->peer_count() == 1);
    assert(tunnel->generation() == 1);

    auto duplicate = tunnel->add_peer(peer_a());
    assert(!duplicate.has_value());
    assert(duplicate.error() == cntp::WireguardError::DuplicatePeer);

    auto added = tunnel->add_peer(peer_b());
    assert(added.has_value());
    assert(tunnel->peer_count() == 2);
    assert(tunnel->generation() == 2);

    auto full = tunnel->add_peer(peer_b());
    assert(!full.has_value());
    assert(full.error() == cntp::WireguardError::DuplicatePeer);

    auto handle = tunnel->plan_handle();
    assert(handle.has_value());
    assert(handle->peek().generation == tunnel->generation());

    auto removed = tunnel->remove_peer(pub(kPeerA));
    assert(removed.has_value());
    assert(tunnel->peer_count() == 1);
    assert(tunnel->generation() == 3);

    auto missing = tunnel->remove_peer(pub(kPeerA));
    assert(!missing.has_value());
    assert(missing.error() == cntp::WireguardError::PeerNotFound);

    std::printf("  test_tunnel_plan_mutation: PASSED\n");
}

}  // namespace

int main() {
    static_assert(sizeof(cntp::DeclaredWireguardPublicKey)
                  == sizeof(cntp::WireguardKeyB64));
    static_assert(sizeof(cntp::WireguardPort) == sizeof(std::uint16_t));
    static_assert(sizeof(cntp::WireguardCidrPrefix) == sizeof(std::uint8_t));
    static_assert(sizeof(cntp::OwnedWireguardTunnel)
                  == sizeof(cntp::WireguardTunnelHandle));
    static_assert(std::same_as<
                  cntp::DeclaredWireguardPublicKey::tag_type,
                  saf::source::Wireguard>);
    static_assert(!std::copy_constructible<cntp::WireguardSecretKeyBytes>);
    static_assert(!std::copy_constructible<cntp::WireguardConfig>);
    static_assert(cntp::CtxFitsWireguardMint<eff::ColdInitCtx>);
    static_assert(!cntp::CtxFitsWireguardMint<eff::BgDrainCtx>);

    std::printf("test_cntp_wireguard:\n");
    test_admission_and_names();
    test_config_and_backend_boundary();
    test_preshared_key_and_endpoint_validation();
    test_tunnel_plan_mutation();
    std::printf("test_cntp_wireguard: all PASSED\n");
    return 0;
}
