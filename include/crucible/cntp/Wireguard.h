#pragma once

// GAPS-160. CNT-P WireGuard site-to-site transport substrate.
//
// This header owns typed admission for WireGuard keys, peers, allowed-IP
// routes, and tunnel plans. It deliberately does not invoke wg(8), netlink,
// rtnetlink, CAP_NET_ADMIN paths, or the Linux WireGuard kernel module.
// Backend operations validate the typed facts and report explicit
// unavailability instead of fabricating a live tunnel.

#include <crucible/cntp/Pacing.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/safety/Linear.h>
#include <crucible/safety/Pinned.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/RefinedAlgebra.h>
#include <crucible/safety/Secret.h>
#include <crucible/safety/Tagged.h>

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::cntp {

inline constexpr std::uint8_t kWireguardKeyBase64Bytes = 44;
inline constexpr std::uint8_t kWireguardMaxAllowedIps = 8;
inline constexpr std::uint8_t kWireguardMaxPeers = 16;

enum class WireguardError : std::uint8_t {
    EmptyKey,
    InvalidKeySize,
    InvalidKeyEncoding,
    InvalidPort,
    InvalidEndpoint,
    InvalidCidrPrefix,
    EmptyAllowedIpSet,
    TooManyAllowedIps,
    EmptyPeerSet,
    TooManyPeers,
    DuplicatePeer,
    PeerNotFound,
    BackendUnavailable,
};

[[nodiscard]] std::string_view
wireguard_error_name(WireguardError error) noexcept;

using WireguardPort =
    safety::Bounded<std::uint16_t{1}, std::uint16_t{65'535}, std::uint16_t>;
using WireguardCidrPrefix =
    safety::Bounded<std::uint8_t{0}, std::uint8_t{32}, std::uint8_t>;

struct WireguardKeyB64 {
    std::array<char, kWireguardKeyBase64Bytes> bytes{};

    [[nodiscard]] constexpr std::string_view view() const noexcept {
        return {bytes.data(), bytes.size()};
    }
};

struct WireguardSecretKeyBytes {
    std::array<char, kWireguardKeyBase64Bytes> bytes{};
    std::uint8_t nbytes = 0;

    constexpr WireguardSecretKeyBytes() noexcept = default;
    WireguardSecretKeyBytes(WireguardSecretKeyBytes const&) = delete;
    WireguardSecretKeyBytes& operator=(WireguardSecretKeyBytes const&) = delete;

    constexpr WireguardSecretKeyBytes(WireguardSecretKeyBytes&& other) noexcept
        : bytes{other.bytes}, nbytes{other.nbytes} {
        other.zeroize();
    }

    constexpr WireguardSecretKeyBytes&
    operator=(WireguardSecretKeyBytes&& other) noexcept {
        if (this != &other) {
            zeroize();
            bytes = other.bytes;
            nbytes = other.nbytes;
            other.zeroize();
        }
        return *this;
    }

    constexpr ~WireguardSecretKeyBytes() noexcept { zeroize(); }

    [[nodiscard]] constexpr std::string_view view() const noexcept {
        return {bytes.data(), nbytes};
    }

    [[nodiscard]] constexpr std::size_t size_bytes() const noexcept {
        return nbytes;
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept {
        return nbytes;
    }

    constexpr void zeroize() noexcept {
        for (char& b : bytes) {
            b = '\0';
        }
        nbytes = 0;
    }
};

using DeclaredWireguardPublicKey =
    safety::Tagged<WireguardKeyB64, safety::source::Wireguard>;
using WireguardSecretKey = safety::Secret<WireguardSecretKeyBytes>;

struct WireguardEndpoint {
    std::uint32_t ipv4_be = 0;
    WireguardPort port{std::uint16_t{1},
                       typename WireguardPort::Trusted{}};
};

struct WireguardAllowedIp {
    std::uint32_t ipv4_be = 0;
    WireguardCidrPrefix prefix_bits{
        std::uint8_t{32}, typename WireguardCidrPrefix::Trusted{}};
};

struct WireguardPeer {
    DeclaredWireguardPublicKey public_key{WireguardKeyB64{}};
    WireguardEndpoint endpoint{};
    std::array<WireguardAllowedIp, kWireguardMaxAllowedIps> allowed_ips{};
    std::uint8_t allowed_ip_count = 0;
    bool persistent_keepalive = false;
};

using DeclaredWireguardPeer =
    safety::Tagged<WireguardPeer, safety::source::Wireguard>;

struct WireguardTunnelHandle {
    NicInterfaceName interface{};
    DeclaredWireguardPublicKey public_key{WireguardKeyB64{}};
    std::uint32_t generation = 1;
};

using OwnedWireguardTunnel = safety::Linear<WireguardTunnelHandle>;

struct WireguardConfig {
    NicInterfaceName interface{};
    WireguardPort listen_port{std::uint16_t{1},
                              typename WireguardPort::Trusted{}};
    WireguardSecretKey private_key;
    WireguardSecretKey preshared_key;
    bool has_preshared_key = false;
    std::array<WireguardPeer, kWireguardMaxPeers> peers{};
    std::uint8_t peer_count = 0;

    constexpr WireguardConfig(NicInterfaceName iface,
                              WireguardPort port,
                              WireguardSecretKey private_key_in,
                              WireguardSecretKey preshared_key_in,
                              bool has_psk) noexcept
        : interface{iface},
          listen_port{port},
          private_key{std::move(private_key_in)},
          preshared_key{std::move(preshared_key_in)},
          has_preshared_key{has_psk} {}

    WireguardConfig(WireguardConfig const&) = delete;
    WireguardConfig& operator=(WireguardConfig const&) = delete;
    WireguardConfig(WireguardConfig&&) = default;
    WireguardConfig& operator=(WireguardConfig&&) = default;
    ~WireguardConfig() = default;
};

using DeclaredWireguardConfig =
    safety::Tagged<WireguardConfig, safety::source::Wireguard>;

template <class Ctx>
concept CtxFitsWireguardMint =
    effects::IsExecCtx<Ctx>
    && effects::CtxAdmits<Ctx, effects::Row<effects::Effect::Init>>;

template <std::size_t N>
concept WireguardAllowedIpShape =
    N > 0u && N <= kWireguardMaxAllowedIps;

template <std::size_t N>
concept WireguardPeerSetShape =
    N > 0u && N <= kWireguardMaxPeers;

[[nodiscard]] constexpr bool
wireguard_key_char_ok(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') ||
           c == '+' ||
           c == '/' ||
           c == '=';
}

[[nodiscard]] constexpr std::expected<void, WireguardError>
validate_wireguard_key_text(std::string_view key) noexcept {
    if (key.empty()) {
        return std::unexpected(WireguardError::EmptyKey);
    }
    if (key.size() != kWireguardKeyBase64Bytes) {
        return std::unexpected(WireguardError::InvalidKeySize);
    }
    if (key.back() != '=' || key[key.size() - 2u] == '=') {
        return std::unexpected(WireguardError::InvalidKeyEncoding);
    }
    for (std::size_t i = 0; i < key.size(); ++i) {
        if (!wireguard_key_char_ok(key[i])) {
            return std::unexpected(WireguardError::InvalidKeyEncoding);
        }
        if (key[i] == '=' && i + 1u < key.size()) {
            return std::unexpected(WireguardError::InvalidKeyEncoding);
        }
    }
    return {};
}

[[nodiscard]] constexpr std::expected<DeclaredWireguardPublicKey, WireguardError>
admit_wireguard_public_key_b64(std::string_view key) noexcept {
    auto valid = validate_wireguard_key_text(key);
    if (!valid.has_value()) {
        return std::unexpected(valid.error());
    }
    WireguardKeyB64 out{};
    for (std::size_t i = 0; i < key.size(); ++i) {
        out.bytes[i] = key[i];
    }
    return DeclaredWireguardPublicKey{out};
}

[[nodiscard]] constexpr std::expected<WireguardSecretKey, WireguardError>
admit_wireguard_secret_key_b64(std::string_view key) noexcept {
    auto valid = validate_wireguard_key_text(key);
    if (!valid.has_value()) {
        return std::unexpected(valid.error());
    }
    WireguardSecretKeyBytes out{};
    for (std::size_t i = 0; i < key.size(); ++i) {
        out.bytes[i] = key[i];
    }
    out.nbytes = static_cast<std::uint8_t>(key.size());
    return WireguardSecretKey{std::move(out)};
}

[[nodiscard]] constexpr WireguardSecretKey
empty_wireguard_secret_key() noexcept {
    return WireguardSecretKey{WireguardSecretKeyBytes{}};
}

[[nodiscard]] constexpr std::expected<WireguardPort, WireguardError>
admit_wireguard_port(std::uint16_t port) noexcept {
    if (port == 0u) {
        return std::unexpected(WireguardError::InvalidPort);
    }
    return WireguardPort{port, typename WireguardPort::Trusted{}};
}

[[nodiscard]] constexpr std::expected<WireguardCidrPrefix, WireguardError>
admit_wireguard_cidr_prefix(std::uint8_t prefix) noexcept {
    if (prefix > 32u) {
        return std::unexpected(WireguardError::InvalidCidrPrefix);
    }
    return WireguardCidrPrefix{
        prefix, typename WireguardCidrPrefix::Trusted{}};
}

[[nodiscard]] constexpr bool
same_wireguard_key(WireguardKeyB64 const& lhs,
                   WireguardKeyB64 const& rhs) noexcept {
    unsigned diff = 0;
    for (std::size_t i = 0; i < lhs.bytes.size(); ++i) {
        diff |= static_cast<unsigned>(
            static_cast<unsigned char>(lhs.bytes[i]) ^
            static_cast<unsigned char>(rhs.bytes[i]));
    }
    return diff == 0u;
}

template <std::size_t N>
    requires WireguardAllowedIpShape<N>
[[nodiscard]] constexpr DeclaredWireguardPeer
declare_wireguard_peer(DeclaredWireguardPublicKey public_key,
                       WireguardEndpoint endpoint,
                       std::array<WireguardAllowedIp, N> allowed_ips,
                       bool persistent_keepalive = false) noexcept {
    WireguardPeer peer{
        .public_key = public_key,
        .endpoint = endpoint,
        .persistent_keepalive = persistent_keepalive,
    };
    for (std::size_t i = 0; i < N; ++i) {
        peer.allowed_ips[i] = allowed_ips[i];
    }
    peer.allowed_ip_count = static_cast<std::uint8_t>(N);
    return DeclaredWireguardPeer{peer};
}

[[nodiscard]] constexpr std::expected<void, WireguardError>
validate_wireguard_endpoint(WireguardEndpoint endpoint) noexcept {
    if (endpoint.ipv4_be == 0u) {
        return std::unexpected(WireguardError::InvalidEndpoint);
    }
    if (endpoint.port.value() == 0u) {
        return std::unexpected(WireguardError::InvalidPort);
    }
    return {};
}

[[nodiscard]] constexpr std::expected<void, WireguardError>
validate_wireguard_peer(WireguardPeer const& peer) noexcept {
    if (peer.allowed_ip_count == 0u) {
        return std::unexpected(WireguardError::EmptyAllowedIpSet);
    }
    if (peer.allowed_ip_count > kWireguardMaxAllowedIps) {
        return std::unexpected(WireguardError::TooManyAllowedIps);
    }
    auto endpoint_valid = validate_wireguard_endpoint(peer.endpoint);
    if (!endpoint_valid.has_value()) {
        return std::unexpected(endpoint_valid.error());
    }
    return {};
}

template <std::size_t PeerCount>
    requires WireguardPeerSetShape<PeerCount>
[[nodiscard]] constexpr std::expected<void, WireguardError>
copy_wireguard_peers(
    WireguardConfig& config,
    std::array<DeclaredWireguardPeer, PeerCount> const& peers) noexcept {
    for (std::size_t i = 0; i < PeerCount; ++i) {
        auto const& peer = peers[i].value();
        auto valid = validate_wireguard_peer(peer);
        if (!valid.has_value()) {
            return std::unexpected(valid.error());
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (same_wireguard_key(config.peers[j].public_key.value(),
                                   peer.public_key.value())) {
                return std::unexpected(WireguardError::DuplicatePeer);
            }
        }
        config.peers[i] = peer;
    }
    config.peer_count = static_cast<std::uint8_t>(PeerCount);
    return {};
}

template <std::size_t PeerCount>
    requires WireguardPeerSetShape<PeerCount>
[[nodiscard]] constexpr std::expected<DeclaredWireguardConfig, WireguardError>
mint_wireguard_config(NicInterfaceName iface,
                      WireguardPort listen_port,
                      WireguardSecretKey private_key,
                      std::array<DeclaredWireguardPeer, PeerCount> peers) noexcept {
    WireguardConfig config{
        iface, listen_port, std::move(private_key),
        empty_wireguard_secret_key(), false};
    auto copied = copy_wireguard_peers(config, peers);
    if (!copied.has_value()) {
        return std::unexpected(copied.error());
    }
    return DeclaredWireguardConfig{std::move(config)};
}

template <std::size_t PeerCount>
    requires WireguardPeerSetShape<PeerCount>
[[nodiscard]] constexpr std::expected<DeclaredWireguardConfig, WireguardError>
mint_wireguard_config_with_psk(
    NicInterfaceName iface,
    WireguardPort listen_port,
    WireguardSecretKey private_key,
    WireguardSecretKey preshared_key,
    std::array<DeclaredWireguardPeer, PeerCount> peers) noexcept {
    if (preshared_key.size() == 0u) {
        return std::unexpected(WireguardError::EmptyKey);
    }
    WireguardConfig config{
        iface, listen_port, std::move(private_key),
        std::move(preshared_key), true};
    auto copied = copy_wireguard_peers(config, peers);
    if (!copied.has_value()) {
        return std::unexpected(copied.error());
    }
    return DeclaredWireguardConfig{std::move(config)};
}

[[nodiscard]] constexpr std::expected<void, WireguardError>
validate_wireguard_config(DeclaredWireguardConfig const& config) noexcept {
    auto const& raw = config.value();
    if (raw.private_key.size() == 0u) {
        return std::unexpected(WireguardError::EmptyKey);
    }
    if (raw.listen_port.value() == 0u) {
        return std::unexpected(WireguardError::InvalidPort);
    }
    if (raw.has_preshared_key && raw.preshared_key.size() == 0u) {
        return std::unexpected(WireguardError::EmptyKey);
    }
    if (raw.peer_count == 0u) {
        return std::unexpected(WireguardError::EmptyPeerSet);
    }
    if (raw.peer_count > kWireguardMaxPeers) {
        return std::unexpected(WireguardError::TooManyPeers);
    }
    for (std::uint8_t i = 0; i < raw.peer_count; ++i) {
        auto valid = validate_wireguard_peer(raw.peers[i]);
        if (!valid.has_value()) {
            return std::unexpected(valid.error());
        }
    }
    return {};
}

template <std::uint8_t MaxPeers = kWireguardMaxPeers>
    requires (MaxPeers > 0u && MaxPeers <= kWireguardMaxPeers)
class WireguardTunnel : public safety::Pinned<WireguardTunnel<MaxPeers>> {
    WireguardConfig config_;
    std::uint32_t generation_ = 1;

    [[nodiscard]] constexpr std::uint8_t
    peer_index(DeclaredWireguardPublicKey public_key) const noexcept {
        for (std::uint8_t i = 0; i < config_.peer_count; ++i) {
            if (same_wireguard_key(config_.peers[i].public_key.value(),
                                   public_key.value())) {
                return i;
            }
        }
        return UINT8_MAX;
    }

public:
    explicit constexpr WireguardTunnel(DeclaredWireguardConfig config) noexcept
        : config_{std::move(config).into()} {}

    [[nodiscard]] constexpr NicInterfaceName interface() const noexcept {
        return config_.interface;
    }

    [[nodiscard]] constexpr std::uint8_t peer_count() const noexcept {
        return config_.peer_count;
    }

    [[nodiscard]] constexpr std::uint32_t generation() const noexcept {
        return generation_;
    }

    [[nodiscard]] constexpr std::expected<void, WireguardError>
    add_peer(DeclaredWireguardPeer peer) noexcept {
        auto valid = validate_wireguard_peer(peer.value());
        if (!valid.has_value()) {
            return std::unexpected(valid.error());
        }
        if (peer_index(peer.value().public_key) != UINT8_MAX) {
            return std::unexpected(WireguardError::DuplicatePeer);
        }
        if (config_.peer_count >= MaxPeers ||
            config_.peer_count >= kWireguardMaxPeers) {
            return std::unexpected(WireguardError::TooManyPeers);
        }
        config_.peers[config_.peer_count] = peer.value();
        ++config_.peer_count;
        ++generation_;
        return {};
    }

    [[nodiscard]] constexpr std::expected<void, WireguardError>
    remove_peer(DeclaredWireguardPublicKey public_key) noexcept {
        std::uint8_t idx = peer_index(public_key);
        if (idx == UINT8_MAX) {
            return std::unexpected(WireguardError::PeerNotFound);
        }
        for (std::uint8_t i = idx; i + 1u < config_.peer_count; ++i) {
            config_.peers[i] = config_.peers[i + 1u];
        }
        --config_.peer_count;
        ++generation_;
        return {};
    }

    [[nodiscard]] constexpr std::expected<OwnedWireguardTunnel, WireguardError>
    plan_handle() const noexcept {
        if (config_.peer_count == 0u) {
            return std::unexpected(WireguardError::EmptyPeerSet);
        }
        return OwnedWireguardTunnel{WireguardTunnelHandle{
            .interface = config_.interface,
            .public_key = config_.peers[0].public_key,
            .generation = generation_,
        }};
    }
};

template <std::uint8_t MaxPeers = kWireguardMaxPeers, class Ctx>
    requires CtxFitsWireguardMint<Ctx>
[[nodiscard]] constexpr std::expected<WireguardTunnel<MaxPeers>, WireguardError>
mint_wireguard_tunnel(Ctx const&, DeclaredWireguardConfig config) noexcept {
    auto valid = validate_wireguard_config(config);
    if (!valid.has_value()) {
        return std::unexpected(valid.error());
    }
    if (config.value().peer_count > MaxPeers) {
        return std::unexpected(WireguardError::TooManyPeers);
    }
    return std::expected<WireguardTunnel<MaxPeers>, WireguardError>{
        std::in_place, std::move(config)};
}

[[nodiscard]] std::expected<OwnedWireguardTunnel, WireguardError>
bring_up_wireguard(DeclaredWireguardConfig const& config) noexcept;

[[nodiscard]] std::expected<void, WireguardError>
apply_wireguard_peer_add(DeclaredWireguardConfig const& config,
                         DeclaredWireguardPeer peer) noexcept;

[[nodiscard]] std::expected<void, WireguardError>
apply_wireguard_peer_remove(DeclaredWireguardConfig const& config,
                            DeclaredWireguardPublicKey peer) noexcept;

static_assert(sizeof(DeclaredWireguardPublicKey) == sizeof(WireguardKeyB64));
static_assert(sizeof(WireguardPort) == sizeof(std::uint16_t));
static_assert(sizeof(WireguardCidrPrefix) == sizeof(std::uint8_t));
static_assert(sizeof(OwnedWireguardTunnel) == sizeof(WireguardTunnelHandle));
static_assert(!std::copy_constructible<WireguardSecretKeyBytes>);
static_assert(!std::copy_constructible<WireguardConfig>);
static_assert(std::move_constructible<WireguardConfig>);
static_assert(std::is_trivially_copyable_v<WireguardKeyB64>);
static_assert(std::is_trivially_copyable_v<WireguardEndpoint>);
static_assert(std::is_trivially_copyable_v<WireguardAllowedIp>);
static_assert(std::is_trivially_copyable_v<WireguardPeer>);
static_assert(CtxFitsWireguardMint<effects::ColdInitCtx>);
static_assert(!CtxFitsWireguardMint<effects::BgDrainCtx>);

}  // namespace crucible::cntp
