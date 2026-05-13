#include <crucible/cntp/Wireguard.h>

namespace crucible::cntp {

std::string_view wireguard_error_name(WireguardError error) noexcept {
    switch (error) {
        case WireguardError::EmptyKey:             return "EmptyKey";
        case WireguardError::InvalidKeySize:      return "InvalidKeySize";
        case WireguardError::InvalidKeyEncoding:  return "InvalidKeyEncoding";
        case WireguardError::InvalidPort:         return "InvalidPort";
        case WireguardError::InvalidEndpoint:     return "InvalidEndpoint";
        case WireguardError::InvalidCidrPrefix:   return "InvalidCidrPrefix";
        case WireguardError::EmptyAllowedIpSet:   return "EmptyAllowedIpSet";
        case WireguardError::TooManyAllowedIps:   return "TooManyAllowedIps";
        case WireguardError::EmptyPeerSet:        return "EmptyPeerSet";
        case WireguardError::TooManyPeers:        return "TooManyPeers";
        case WireguardError::DuplicatePeer:       return "DuplicatePeer";
        case WireguardError::PeerNotFound:        return "PeerNotFound";
        case WireguardError::BackendUnavailable:  return "BackendUnavailable";
        default:                                  return "<unknown WireguardError>";
    }
}

std::expected<OwnedWireguardTunnel, WireguardError>
bring_up_wireguard(DeclaredWireguardConfig const& config) noexcept {
    auto valid = validate_wireguard_config(config);
    if (!valid.has_value()) {
        return std::unexpected(valid.error());
    }
    return std::unexpected(WireguardError::BackendUnavailable);
}

std::expected<void, WireguardError>
apply_wireguard_peer_add(DeclaredWireguardConfig const& config,
                         DeclaredWireguardPeer peer) noexcept {
    auto config_valid = validate_wireguard_config(config);
    if (!config_valid.has_value()) {
        return std::unexpected(config_valid.error());
    }
    auto peer_valid = validate_wireguard_peer(peer.value());
    if (!peer_valid.has_value()) {
        return std::unexpected(peer_valid.error());
    }
    return std::unexpected(WireguardError::BackendUnavailable);
}

std::expected<void, WireguardError>
apply_wireguard_peer_remove(DeclaredWireguardConfig const& config,
                            DeclaredWireguardPublicKey peer) noexcept {
    auto config_valid = validate_wireguard_config(config);
    if (!config_valid.has_value()) {
        return std::unexpected(config_valid.error());
    }
    for (std::uint8_t i = 0; i < config.value().peer_count; ++i) {
        if (same_wireguard_key(config.value().peers[i].public_key.value(),
                               peer.value())) {
            return std::unexpected(WireguardError::BackendUnavailable);
        }
    }
    return std::unexpected(WireguardError::PeerNotFound);
}

}  // namespace crucible::cntp
