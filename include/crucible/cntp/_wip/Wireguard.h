#pragma once

// The backend operations declared at the bottom of this header invoke no
// wg(8), no netlink, no rtnetlink, no CAP_NET_ADMIN path and not the kernel
// WireGuard module.  They validate the typed facts and report unavailability
// rather than fabricate a live tunnel.

#include <crucible/cntp/Pacing.h>
#include <fixy/Ctx.h>
#include <fixy/Qtt.h>
#include <fixy/Refined.h>
#include <fixy/Secret.h>
#include <fixy/Tagged.h>
#include <foundation/Pinned.h>
#include <foundation/effects/Ctx.h>

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <inplace_vector>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::cntp::_wip {

using ::crucible::cntp::NicInterfaceName;

namespace wip_source {
struct Wireguard {};
}  // namespace wip_source

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

[[nodiscard]] std::string_view wireguard_error_name(WireguardError error) noexcept;

// The bounds, stated once.  The refined types, their admission doors and
// the defaults all read these predicates.  Port zero asks the kernel to
// pick a port, so a configured port is not zero.
inline constexpr auto wireguard_port_range = ::fixy::in_range<std::uint16_t{1}, std::uint16_t{65535}>;
inline constexpr auto wireguard_cidr_prefix_range = ::fixy::in_range<std::uint8_t{0}, std::uint8_t{32}>;

using WireguardPort = ::fixy::Refined<wireguard_port_range, std::uint16_t>;
using WireguardCidrPrefix = ::fixy::Refined<wireguard_cidr_prefix_range, std::uint8_t>;

[[nodiscard]] constexpr bool wireguard_key_char_ok(char c) noexcept {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '/'
        || c == '=';
}

// A key is 44 characters of base64 that end in one padding character.
// O(n) in the length of the text.
[[nodiscard]] constexpr std::expected<void, WireguardError> validate_wireguard_key_text(std::string_view key) noexcept {
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

using WireguardKeyChars = std::array<char, kWireguardKeyBase64Bytes>;

// The key text rule as a predicate, so a public key carries the rule in its
// type.  No value of the key type holds a text that the rule refuses.
struct IsWireguardKeyText {
    constexpr bool operator()(WireguardKeyChars const& chars) const noexcept {
        return validate_wireguard_key_text(std::string_view{chars.data(), chars.size()}).has_value();
    }
};

inline constexpr IsWireguardKeyText wireguard_key_text{};

using WireguardKeyB64 = ::fixy::Refined<wireguard_key_text, WireguardKeyChars>;

// admit_wireguard_public_key_b64 is the one function that returns a
// declared public key.  The key has no default, so no empty slot holds a
// key that the rule never saw.
using DeclaredWireguardPublicKey = ::fixy::Tagged<WireguardKeyB64, wip_source::Wireguard>;

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

    constexpr WireguardSecretKeyBytes& operator=(WireguardSecretKeyBytes&& other) noexcept {
        if (this != &other) {
            zeroize();
            bytes = other.bytes;
            nbytes = other.nbytes;
            other.zeroize();
        }
        return *this;
    }

    constexpr ~WireguardSecretKeyBytes() noexcept { zeroize(); }

    [[nodiscard]] constexpr std::string_view view() const noexcept { return {bytes.data(), nbytes}; }

    [[nodiscard]] constexpr std::size_t size_bytes() const noexcept { return nbytes; }

    [[nodiscard]] constexpr std::size_t size() const noexcept { return nbytes; }

    constexpr void zeroize() noexcept {
        for (char& b : bytes) {
            b = '\0';
        }
        nbytes = 0;
    }
};

using WireguardSecretKey = ::fixy::Secret<WireguardSecretKeyBytes>;

// The port takes no default, so an endpoint names the port that its caller
// admitted.
struct WireguardEndpoint {
    std::uint32_t ipv4_be = 0;
    WireguardPort port;
};

struct WireguardAllowedIp {
    std::uint32_t ipv4_be = 0;
    WireguardCidrPrefix prefix_bits = ::fixy::mint_refined<wireguard_cidr_prefix_range>(std::uint8_t{32});
};

struct WireguardPeer {
    DeclaredWireguardPublicKey public_key;
    WireguardEndpoint endpoint;
    std::array<WireguardAllowedIp, kWireguardMaxAllowedIps> allowed_ips{};
    std::uint8_t allowed_ip_count = 0;
    bool persistent_keepalive = false;
};

// declare_wireguard_peer is the one function that returns a declared peer.
using DeclaredWireguardPeer = ::fixy::Tagged<WireguardPeer, wip_source::Wireguard>;

template <std::uint8_t MaxPeers>
    requires(MaxPeers > 0u && MaxPeers <= kWireguardMaxPeers)
class WireguardTunnel;

// The handle of a tunnel.  The constructor is private and a tunnel plan is
// its one door.  A handle names one tunnel, so it moves and does not copy:
// a copy would let a second owner be minted from the first.
class WireguardTunnelHandle {
public:
    WireguardTunnelHandle(WireguardTunnelHandle const&) = delete("a tunnel handle names one tunnel");
    WireguardTunnelHandle& operator=(WireguardTunnelHandle const&) = delete("a tunnel handle names one tunnel");
    WireguardTunnelHandle(WireguardTunnelHandle&&) noexcept = default;
    WireguardTunnelHandle& operator=(WireguardTunnelHandle&&) noexcept = default;
    ~WireguardTunnelHandle() = default;

    [[nodiscard]] constexpr NicInterfaceName interface() const noexcept { return interface_; }
    [[nodiscard]] constexpr DeclaredWireguardPublicKey const& public_key() const noexcept { return public_key_; }
    [[nodiscard]] constexpr std::uint32_t generation() const noexcept { return generation_; }

private:
    constexpr WireguardTunnelHandle(NicInterfaceName interface, DeclaredWireguardPublicKey public_key,
                                    std::uint32_t generation) noexcept
        : interface_{interface}, public_key_{public_key}, generation_{generation} {}

    template <std::uint8_t MaxPeers>
        requires(MaxPeers > 0u && MaxPeers <= kWireguardMaxPeers)
    friend class WireguardTunnel;

    NicInterfaceName interface_{};
    DeclaredWireguardPublicKey public_key_;
    std::uint32_t generation_ = 1;
};

using OwnedWireguardTunnel = ::fixy::Linear<WireguardTunnelHandle>;

// The peers sit in a bounded vector, so the config holds exactly the peers
// that its mint copied and no empty slot.
struct WireguardConfig {
    NicInterfaceName interface{};
    WireguardPort listen_port;
    WireguardSecretKey private_key;
    WireguardSecretKey preshared_key;
    bool has_preshared_key = false;
    std::inplace_vector<WireguardPeer, kWireguardMaxPeers> peers;

    constexpr WireguardConfig(NicInterfaceName iface, WireguardPort port, WireguardSecretKey private_key_in,
                              WireguardSecretKey preshared_key_in, bool has_psk) noexcept
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

// mint_wireguard_config and mint_wireguard_config_with_psk are the one
// functions that return a declared config.
using DeclaredWireguardConfig = ::fixy::Tagged<WireguardConfig, wip_source::Wireguard>;

template <class Ctx>
concept CtxFitsWireguardMint = ::foundation::effects::IsExecCtx<Ctx>
                            && ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Init>;

template <std::size_t N>
concept WireguardAllowedIpShape = N > 0u && N <= kWireguardMaxAllowedIps;

template <std::size_t N>
concept WireguardPeerSetShape = N > 0u && N <= kWireguardMaxPeers;

[[nodiscard]] constexpr std::expected<DeclaredWireguardPublicKey, WireguardError>
admit_wireguard_public_key_b64(std::string_view key) noexcept {
    auto valid = validate_wireguard_key_text(key);
    if (!valid.has_value()) {
        return std::unexpected(valid.error());
    }
    WireguardKeyChars chars{};
    for (std::size_t i = 0; i < key.size(); ++i) {
        chars[i] = key[i];
    }
    return ::fixy::mint_tagged<wip_source::Wireguard>(::fixy::mint_refined<wireguard_key_text>(chars));
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
    return ::fixy::mint_secret<WireguardSecretKeyBytes>(std::move(out));
}

[[nodiscard]] constexpr WireguardSecretKey empty_wireguard_secret_key() noexcept {
    return ::fixy::mint_secret<WireguardSecretKeyBytes>();
}

[[nodiscard]] constexpr std::expected<WireguardPort, WireguardError> admit_wireguard_port(std::uint16_t port) noexcept {
    return ::fixy::admit_refined<wireguard_port_range>(port, WireguardError::InvalidPort);
}

[[nodiscard]] constexpr std::expected<WireguardCidrPrefix, WireguardError>
admit_wireguard_cidr_prefix(std::uint8_t prefix) noexcept {
    return ::fixy::admit_refined<wireguard_cidr_prefix_range>(prefix, WireguardError::InvalidCidrPrefix);
}

// The comparison reads every byte, so its time does not depend on where two
// keys first differ.
[[nodiscard]] constexpr bool same_wireguard_key(DeclaredWireguardPublicKey const& lhs,
                                                DeclaredWireguardPublicKey const& rhs) noexcept {
    WireguardKeyChars const& left = lhs.value().value();
    WireguardKeyChars const& right = rhs.value().value();
    unsigned diff = 0;
    for (std::size_t i = 0; i < left.size(); ++i) {
        diff |= static_cast<unsigned>(static_cast<unsigned char>(left[i]) ^ static_cast<unsigned char>(right[i]));
    }
    return diff == 0u;
}

template <std::size_t N>
    requires WireguardAllowedIpShape<N>
[[nodiscard]] constexpr DeclaredWireguardPeer
declare_wireguard_peer(DeclaredWireguardPublicKey public_key, WireguardEndpoint endpoint,
                       std::array<WireguardAllowedIp, N> allowed_ips, bool persistent_keepalive = false) noexcept {
    WireguardPeer peer{
        .public_key = public_key,
        .endpoint = endpoint,
        .persistent_keepalive = persistent_keepalive,
    };
    for (std::size_t i = 0; i < N; ++i) {
        peer.allowed_ips[i] = allowed_ips[i];
    }
    peer.allowed_ip_count = static_cast<std::uint8_t>(N);
    return ::fixy::mint_tagged<wip_source::Wireguard>(peer);
}

// The refined port already holds this bound.  The check reads it again, so
// a port that entered through mint_refined_trusted is still refused here.
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
    return validate_wireguard_endpoint(peer.endpoint);
}

// O(n²) in the number of peers, which is at most kWireguardMaxPeers.
template <std::size_t PeerCount>
    requires WireguardPeerSetShape<PeerCount>
[[nodiscard]] constexpr std::expected<void, WireguardError>
copy_wireguard_peers(WireguardConfig& config, std::array<DeclaredWireguardPeer, PeerCount> const& peers) noexcept {
    for (DeclaredWireguardPeer const& declared : peers) {
        WireguardPeer const& peer = declared.value();
        auto valid = validate_wireguard_peer(peer);
        if (!valid.has_value()) {
            return valid;
        }
        for (WireguardPeer const& held : config.peers) {
            if (same_wireguard_key(held.public_key, peer.public_key)) {
                return std::unexpected(WireguardError::DuplicatePeer);
            }
        }
        config.peers.push_back(peer);
    }
    return {};
}

template <std::size_t PeerCount>
    requires WireguardPeerSetShape<PeerCount>
[[nodiscard]] constexpr std::expected<DeclaredWireguardConfig, WireguardError>
mint_wireguard_config(NicInterfaceName iface, WireguardPort listen_port, WireguardSecretKey private_key,
                      std::array<DeclaredWireguardPeer, PeerCount> peers) noexcept {
    WireguardConfig config{iface, listen_port, std::move(private_key), empty_wireguard_secret_key(), false};
    auto copied = copy_wireguard_peers(config, peers);
    if (!copied.has_value()) {
        return std::unexpected(copied.error());
    }
    return ::fixy::mint_tagged<wip_source::Wireguard>(std::move(config));
}

template <std::size_t PeerCount>
    requires WireguardPeerSetShape<PeerCount>
[[nodiscard]] constexpr std::expected<DeclaredWireguardConfig, WireguardError>
mint_wireguard_config_with_psk(NicInterfaceName iface, WireguardPort listen_port, WireguardSecretKey private_key,
                               WireguardSecretKey preshared_key,
                               std::array<DeclaredWireguardPeer, PeerCount> peers) noexcept {
    if (preshared_key.size() == 0u) {
        return std::unexpected(WireguardError::EmptyKey);
    }
    WireguardConfig config{iface, listen_port, std::move(private_key), std::move(preshared_key), true};
    auto copied = copy_wireguard_peers(config, peers);
    if (!copied.has_value()) {
        return std::unexpected(copied.error());
    }
    return ::fixy::mint_tagged<wip_source::Wireguard>(std::move(config));
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
    if (raw.peers.empty()) {
        return std::unexpected(WireguardError::EmptyPeerSet);
    }
    for (WireguardPeer const& peer : raw.peers) {
        auto valid = validate_wireguard_peer(peer);
        if (!valid.has_value()) {
            return valid;
        }
    }
    return {};
}

template <std::uint8_t MaxPeers = kWireguardMaxPeers, class Ctx>
    requires CtxFitsWireguardMint<Ctx>
[[nodiscard]] constexpr std::expected<WireguardTunnel<MaxPeers>, WireguardError>
mint_wireguard_tunnel(Ctx const&, DeclaredWireguardConfig config) noexcept;

// A tunnel is built only by mint_wireguard_tunnel, which checks the context
// and the config.  The constructor takes a key that only the mint can name,
// so the result is built in place and the tunnel keeps its address.
template <std::uint8_t MaxPeers>
    requires(MaxPeers > 0u && MaxPeers <= kWireguardMaxPeers)
class WireguardTunnel : public ::foundation::Pinned<WireguardTunnel<MaxPeers>> {
    class MintKey {
        explicit constexpr MintKey() noexcept = default;

        template <std::uint8_t Max, class Ctx>
            requires CtxFitsWireguardMint<Ctx>
        friend constexpr std::expected<WireguardTunnel<Max>, WireguardError>
        mint_wireguard_tunnel(Ctx const&, DeclaredWireguardConfig config) noexcept;
    };

    template <std::uint8_t Max, class Ctx>
        requires CtxFitsWireguardMint<Ctx>
    friend constexpr std::expected<WireguardTunnel<Max>, WireguardError>
    mint_wireguard_tunnel(Ctx const&, DeclaredWireguardConfig config) noexcept;

    WireguardConfig config_;
    std::uint32_t generation_ = 1;

    // The index of the peer with this key, or the peer count when no peer
    // has it.  O(n) in the number of peers.
    [[nodiscard]] constexpr std::size_t peer_index(DeclaredWireguardPublicKey const& public_key) const noexcept {
        for (std::size_t i = 0; i < config_.peers.size(); ++i) {
            if (same_wireguard_key(config_.peers[i].public_key, public_key)) {
                return i;
            }
        }
        return config_.peers.size();
    }

public:
    constexpr WireguardTunnel(MintKey, DeclaredWireguardConfig config) noexcept : config_{std::move(config).into()} {}

    [[nodiscard]] constexpr NicInterfaceName interface() const noexcept { return config_.interface; }

    [[nodiscard]] constexpr std::uint8_t peer_count() const noexcept {
        return static_cast<std::uint8_t>(config_.peers.size());
    }

    [[nodiscard]] constexpr std::uint32_t generation() const noexcept { return generation_; }

    [[nodiscard]] constexpr std::expected<void, WireguardError> add_peer(DeclaredWireguardPeer peer) noexcept {
        auto valid = validate_wireguard_peer(peer.value());
        if (!valid.has_value()) {
            return valid;
        }
        if (peer_index(peer.value().public_key) < config_.peers.size()) {
            return std::unexpected(WireguardError::DuplicatePeer);
        }
        if (config_.peers.size() >= MaxPeers) {
            return std::unexpected(WireguardError::TooManyPeers);
        }
        config_.peers.push_back(peer.value());
        ++generation_;
        return {};
    }

    [[nodiscard]] constexpr std::expected<void, WireguardError>
    remove_peer(DeclaredWireguardPublicKey const& public_key) noexcept {
        const std::size_t index = peer_index(public_key);
        if (index >= config_.peers.size()) {
            return std::unexpected(WireguardError::PeerNotFound);
        }
        config_.peers.erase(config_.peers.begin() + static_cast<std::ptrdiff_t>(index));
        ++generation_;
        return {};
    }

    [[nodiscard]] constexpr std::expected<OwnedWireguardTunnel, WireguardError> plan_handle() const noexcept {
        if (config_.peers.empty()) {
            return std::unexpected(WireguardError::EmptyPeerSet);
        }
        return ::fixy::mint_linear<WireguardTunnelHandle>(
            WireguardTunnelHandle{config_.interface, config_.peers.front().public_key, generation_});
    }
};

template <std::uint8_t MaxPeers, class Ctx>
    requires CtxFitsWireguardMint<Ctx>
[[nodiscard]] constexpr std::expected<WireguardTunnel<MaxPeers>, WireguardError>
mint_wireguard_tunnel(Ctx const&, DeclaredWireguardConfig config) noexcept {
    auto valid = validate_wireguard_config(config);
    if (!valid.has_value()) {
        return std::unexpected(valid.error());
    }
    if (config.value().peers.size() > MaxPeers) {
        return std::unexpected(WireguardError::TooManyPeers);
    }
    return std::expected<WireguardTunnel<MaxPeers>, WireguardError>{
        std::in_place, typename WireguardTunnel<MaxPeers>::MintKey{}, std::move(config)};
}

[[nodiscard]] std::expected<OwnedWireguardTunnel, WireguardError>
bring_up_wireguard(DeclaredWireguardConfig const& config) noexcept;

[[nodiscard]] std::expected<void, WireguardError> apply_wireguard_peer_add(DeclaredWireguardConfig const& config,
                                                                           DeclaredWireguardPeer peer) noexcept;

[[nodiscard]] std::expected<void, WireguardError> apply_wireguard_peer_remove(DeclaredWireguardConfig const& config,
                                                                              DeclaredWireguardPublicKey peer) noexcept;

static_assert(sizeof(DeclaredWireguardPublicKey) == sizeof(WireguardKeyChars));
static_assert(sizeof(WireguardPort) == sizeof(std::uint16_t));
static_assert(sizeof(WireguardCidrPrefix) == sizeof(std::uint8_t));
static_assert(::fixy::qtt_consume_tracked || sizeof(OwnedWireguardTunnel) == sizeof(WireguardTunnelHandle));
static_assert(!std::copy_constructible<WireguardSecretKeyBytes>);
static_assert(!std::copy_constructible<WireguardConfig>);
static_assert(std::move_constructible<WireguardConfig>);
// A refined member makes an endpoint, an allowed IP or a peer not trivially
// copyable, because no byte route may build a refined value.  A copy still
// costs what copying the bytes costs.
static_assert(std::is_trivially_copy_constructible_v<WireguardEndpoint>
              && std::is_trivially_destructible_v<WireguardEndpoint>);
static_assert(std::is_trivially_copy_constructible_v<WireguardAllowedIp>
              && std::is_trivially_destructible_v<WireguardAllowedIp>);
static_assert(std::is_trivially_copy_constructible_v<WireguardPeer> && std::is_trivially_destructible_v<WireguardPeer>);
static_assert(CtxFitsWireguardMint<::fixy::ColdInitCtx>);
static_assert(!CtxFitsWireguardMint<::fixy::BgDrainCtx>);
// No key or peer is declared without its door, no handle exists outside a
// tunnel plan, and no tunnel exists outside its mint.
static_assert(!std::is_default_constructible_v<DeclaredWireguardPublicKey>);
static_assert(!std::is_default_constructible_v<DeclaredWireguardPeer>);
static_assert(
    !std::is_constructible_v<WireguardTunnelHandle, NicInterfaceName, DeclaredWireguardPublicKey, std::uint32_t>);
static_assert(!std::is_copy_constructible_v<WireguardTunnelHandle>);
static_assert(!std::is_constructible_v<WireguardTunnel<2>, DeclaredWireguardConfig>);

}  // namespace crucible::cntp::_wip
