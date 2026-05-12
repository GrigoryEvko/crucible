#include <crucible/cntp/MtlsTransport.h>

#include <cstddef>
#include <utility>

namespace crucible::cntp {

std::string_view mtls_error_name(MtlsError error) noexcept {
    switch (error) {
        case MtlsError::EmptyCertificate:             return "EmptyCertificate";
        case MtlsError::CertificateTooLarge:          return "CertificateTooLarge";
        case MtlsError::EmptyPrivateKey:              return "EmptyPrivateKey";
        case MtlsError::PrivateKeyTooLarge:           return "PrivateKeyTooLarge";
        case MtlsError::EmptyPeerName:                return "EmptyPeerName";
        case MtlsError::InvalidPeerName:              return "InvalidPeerName";
        case MtlsError::TooManyPeerNames:             return "TooManyPeerNames";
        case MtlsError::EmptyFingerprint:             return "EmptyFingerprint";
        case MtlsError::UnsupportedTlsVersion:        return "UnsupportedTlsVersion";
        case MtlsError::UnsupportedCipherSuite:       return "UnsupportedCipherSuite";
        case MtlsError::PeerVerificationDisabled:     return "PeerVerificationDisabled";
        case MtlsError::PeerCertificateNotRequired:   return "PeerCertificateNotRequired";
        case MtlsError::PeerNameNotAllowed:           return "PeerNameNotAllowed";
        case MtlsError::CertificatePinMismatch:       return "CertificatePinMismatch";
        case MtlsError::BackendUnavailable:           return "BackendUnavailable";
        case MtlsError::KtlsOffloadDeferred:          return "KtlsOffloadDeferred";
        default:                                      return "<unknown MtlsError>";
    }
}

std::string_view tls_version_name(TlsVersion version) noexcept {
    switch (version) {
        case TlsVersion::V10: return "TLS1.0";
        case TlsVersion::V11: return "TLS1.1";
        case TlsVersion::V12: return "TLS1.2";
        case TlsVersion::V13: return "TLS1.3";
        default:             return "<unknown TLS version>";
    }
}

std::string_view mtls_cipher_suite_name(MtlsCipherSuite suite) noexcept {
    switch (suite) {
        case MtlsCipherSuite::TlsAes256GcmSha384:
            return "TLS_AES_256_GCM_SHA384";
        case MtlsCipherSuite::TlsChacha20Poly1305Sha256:
            return "TLS_CHACHA20_POLY1305_SHA256";
        case MtlsCipherSuite::TlsAes128GcmSha256:
            return "TLS_AES_128_GCM_SHA256";
        case MtlsCipherSuite::LegacyRsa3desSha:
            return "TLS_RSA_WITH_3DES_EDE_CBC_SHA";
        default:
            return "<unknown TLS cipher suite>";
    }
}

std::string_view mtls_key_algorithm_name(MtlsKeyAlgorithm algorithm) noexcept {
    switch (algorithm) {
        case MtlsKeyAlgorithm::Ed25519:  return "ED25519";
        case MtlsKeyAlgorithm::EcdsaP256:return "ECDSA-P256";
        case MtlsKeyAlgorithm::RsaPkcs1: return "RSA-PKCS1";
        default:                         return "<unknown mTLS key algorithm>";
    }
}

std::expected<MtlsCertificate, MtlsError>
admit_x509_certificate_pem(std::span<const std::byte> pem) noexcept {
    if (pem.empty()) {
        return std::unexpected(MtlsError::EmptyCertificate);
    }
    if (pem.size() > MtlsCertificateBytes::max_bytes) {
        return std::unexpected(MtlsError::CertificateTooLarge);
    }

    MtlsCertificateBytes out{};
    for (std::size_t i = 0; i < pem.size(); ++i) {
        out.bytes[i] = pem[i];
    }
    out.size = static_cast<std::uint16_t>(pem.size());
    return std::expected<MtlsCertificate, MtlsError>{
        std::in_place, std::move(out)};
}

namespace {

[[nodiscard]] bool same_fingerprint(MtlsSha256Fingerprint const& lhs,
                                    MtlsSha256Fingerprint const& rhs) noexcept {
    std::byte diff{0};
    for (std::size_t i = 0; i < lhs.bytes.size(); ++i) {
        diff |= lhs.bytes[i] ^ rhs.bytes[i];
    }
    return diff == std::byte{0};
}

[[nodiscard]] bool has_fingerprint(MtlsSha256Fingerprint const& fp) noexcept {
    std::byte acc{0};
    for (std::byte b : fp.bytes) {
        acc |= b;
    }
    return acc != std::byte{0};
}

}  // namespace

std::expected<AuthenticatedMtlsPeer, MtlsError>
admit_mtls_peer_from_handshake(DeclaredMtlsConfig const& config,
                               MtlsDnsName peer_dns,
                               MtlsCertificateFingerprint peer_fingerprint,
                               MtlsCipherSuite chosen_cipher) noexcept {
    auto valid = validate_mtls_config(config);
    if (!valid.has_value()) {
        return std::unexpected(valid.error());
    }

    auto const& policy = config.value().policy;
    if (!mtls_policy_allows_peer_name(policy, peer_dns)) {
        return std::unexpected(MtlsError::PeerNameNotAllowed);
    }
    if (!mtls_policy_allows_cipher(policy, chosen_cipher)) {
        return std::unexpected(MtlsError::UnsupportedCipherSuite);
    }

    for (std::uint8_t i = 0; i < policy.allowed_peer_count; ++i) {
        if (policy.allowed_peer_dns[i].view() == peer_dns.view()) {
            if (has_fingerprint(policy.allowed_peer_pins[i]) &&
                !same_fingerprint(policy.allowed_peer_pins[i],
                                  peer_fingerprint.value())) {
                return std::unexpected(MtlsError::CertificatePinMismatch);
            }
            return AuthenticatedMtlsPeer{MtlsPeerIdentity{
                .dns_name = peer_dns,
                .certificate_sha256 = peer_fingerprint,
                .cipher = chosen_cipher,
            }};
        }
    }
    return std::unexpected(MtlsError::PeerNameNotAllowed);
}

std::expected<MtlsConnection, MtlsError>
connect_mtls(SocketFd socket,
             DeclaredMtlsConfig const& config,
             MtlsDnsName peer_dns,
             MtlsCertificateFingerprint peer_fingerprint) noexcept {
    static_cast<void>(socket);
    auto peer = admit_mtls_peer_from_handshake(
        config,
        peer_dns,
        peer_fingerprint,
        MtlsCipherSuite::TlsAes256GcmSha384);
    if (!peer.has_value()) {
        return std::unexpected(peer.error());
    }
    return std::unexpected(MtlsError::BackendUnavailable);
}

std::expected<std::size_t, MtlsError>
mtls_send(MtlsConnection& connection, std::span<const std::byte> bytes) noexcept {
    static_cast<void>(connection);
    static_cast<void>(bytes);
    return std::unexpected(MtlsError::BackendUnavailable);
}

std::expected<std::size_t, MtlsError>
mtls_recv(MtlsConnection& connection, std::span<std::byte> bytes) noexcept {
    static_cast<void>(connection);
    static_cast<void>(bytes);
    return std::unexpected(MtlsError::BackendUnavailable);
}

std::expected<void, MtlsError>
enable_ktls_offload(MtlsConnection& connection, NicInterfaceName iface) noexcept {
    static_cast<void>(connection);
    static_cast<void>(iface);
    return std::unexpected(MtlsError::KtlsOffloadDeferred);
}

}  // namespace crucible::cntp
