#pragma once

// GAPS-126.  CNT-P mutual-TLS federation transport substrate.
//
// This header owns typed admission for the policy and identity facts that
// a future TLS backend must enforce.  It deliberately does not claim a real
// TLS handshake, OpenSSL/BoringSSL binding, kTLS install, or Cipher audit
// write until those substrates exist.  Runtime connect/send/recv therefore
// report BackendUnavailable rather than fabricating security.

#include <crucible/cntp/CongestionControl.h>
#include <crucible/cntp/Pacing.h>
#include <crucible/safety/Bits.h>
#include <crucible/safety/Linear.h>
#include <crucible/safety/Secret.h>
#include <crucible/safety/Tagged.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::cntp {

enum class MtlsError : std::uint8_t {
    EmptyCertificate,
    CertificateTooLarge,
    EmptyPrivateKey,
    PrivateKeyTooLarge,
    EmptyPeerName,
    InvalidPeerName,
    TooManyPeerNames,
    EmptyFingerprint,
    UnsupportedTlsVersion,
    UnsupportedCipherSuite,
    PeerVerificationDisabled,
    PeerCertificateNotRequired,
    PeerNameNotAllowed,
    CertificatePinMismatch,
    BackendUnavailable,
    KtlsOffloadDeferred,
};

enum class TlsVersion : std::uint8_t {
    V10 = 10,
    V11 = 11,
    V12 = 12,
    V13 = 13,
};

enum class MtlsCipherSuite : std::uint8_t {
    TlsAes256GcmSha384       = 1u << 0,
    TlsChacha20Poly1305Sha256 = 1u << 1,
    TlsAes128GcmSha256       = 1u << 2,
    LegacyRsa3desSha         = 1u << 3,
};

enum class MtlsKeyAlgorithm : std::uint8_t {
    Ed25519,
    EcdsaP256,
    RsaPkcs1,
};

[[nodiscard]] std::string_view mtls_error_name(MtlsError error) noexcept;
[[nodiscard]] std::string_view tls_version_name(TlsVersion version) noexcept;
[[nodiscard]] std::string_view mtls_cipher_suite_name(MtlsCipherSuite suite) noexcept;
[[nodiscard]] std::string_view mtls_key_algorithm_name(MtlsKeyAlgorithm algorithm) noexcept;

template <TlsVersion Version>
concept SupportedMtlsVersion = Version == TlsVersion::V13;

template <MtlsCipherSuite Suite>
concept ApprovedMtlsCipherSuite =
    Suite == MtlsCipherSuite::TlsAes256GcmSha384 ||
    Suite == MtlsCipherSuite::TlsChacha20Poly1305Sha256;

template <MtlsKeyAlgorithm Algorithm>
concept ApprovedMtlsKeyAlgorithm =
    Algorithm == MtlsKeyAlgorithm::Ed25519 ||
    Algorithm == MtlsKeyAlgorithm::EcdsaP256;

using MtlsCipherMask = safety::Bits<MtlsCipherSuite>;

struct MtlsCertificateBytes {
    static constexpr std::size_t max_bytes = 4096;

    std::array<std::byte, max_bytes> bytes{};
    std::uint16_t size = 0;

    [[nodiscard]] constexpr std::span<const std::byte> view() const noexcept {
        return {bytes.data(), size};
    }
};

struct MtlsPrivateKeyBytes {
    static constexpr std::size_t max_bytes = 4096;

    std::array<std::byte, max_bytes> bytes{};
    std::uint16_t nbytes = 0;
    MtlsKeyAlgorithm algorithm = MtlsKeyAlgorithm::Ed25519;

    constexpr MtlsPrivateKeyBytes() noexcept = default;
    MtlsPrivateKeyBytes(MtlsPrivateKeyBytes const&) = delete;
    MtlsPrivateKeyBytes& operator=(MtlsPrivateKeyBytes const&) = delete;

    constexpr MtlsPrivateKeyBytes(MtlsPrivateKeyBytes&& other) noexcept
        : bytes{other.bytes},
          nbytes{other.nbytes},
          algorithm{other.algorithm} {
        other.zeroize();
    }

    constexpr MtlsPrivateKeyBytes& operator=(MtlsPrivateKeyBytes&& other) noexcept {
        if (this != &other) {
            zeroize();
            bytes = other.bytes;
            nbytes = other.nbytes;
            algorithm = other.algorithm;
            other.zeroize();
        }
        return *this;
    }

    constexpr ~MtlsPrivateKeyBytes() noexcept { zeroize(); }

    [[nodiscard]] constexpr std::size_t size_bytes() const noexcept {
        return nbytes;
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept {
        return nbytes;
    }

    constexpr void zeroize() noexcept {
        for (auto& b : bytes) {
            b = std::byte{0};
        }
        nbytes = 0;
        algorithm = MtlsKeyAlgorithm::Ed25519;
    }
};

struct MtlsDnsName {
    static constexpr std::size_t max_bytes = 253;

    std::array<char, max_bytes> bytes{};
    std::uint8_t size = 0;

    [[nodiscard]] constexpr std::string_view view() const noexcept {
        return {bytes.data(), size};
    }

    [[nodiscard]] static constexpr std::expected<MtlsDnsName, MtlsError>
    from(std::string_view name) noexcept {
        if (name.empty()) {
            return std::unexpected(MtlsError::EmptyPeerName);
        }
        if (name.size() > max_bytes || name.front() == '.' ||
            name.back() == '.' || name.front() == '-' || name.back() == '-') {
            return std::unexpected(MtlsError::InvalidPeerName);
        }

        MtlsDnsName out{};
        bool previous_dot = false;
        for (std::size_t i = 0; i < name.size(); ++i) {
            const char c = name[i];
            const bool dot = c == '.';
            const bool ok =
                (c >= 'a' && c <= 'z') ||
                (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') ||
                c == '-' ||
                dot;
            if (!ok || (dot && previous_dot)) {
                return std::unexpected(MtlsError::InvalidPeerName);
            }
            out.bytes[i] = c;
            previous_dot = dot;
        }
        out.size = static_cast<std::uint8_t>(name.size());
        return out;
    }
};

struct MtlsSha256Fingerprint {
    static constexpr std::size_t bytes_count = 32;

    std::array<std::byte, bytes_count> bytes{};
};

using MtlsCertificate = safety::Linear<MtlsCertificateBytes>;
using MtlsPrivateKey = safety::Secret<MtlsPrivateKeyBytes>;
using MtlsCertificateFingerprint =
    safety::Tagged<MtlsSha256Fingerprint, safety::source::Mtls>;

struct MtlsPolicy {
    static constexpr std::size_t max_peer_names = 8;

    bool verify_peer = true;
    bool require_peer_cert = true;
    TlsVersion min_version = TlsVersion::V13;
    TlsVersion max_version = TlsVersion::V13;
    MtlsCipherMask allowed_ciphers{
        MtlsCipherSuite::TlsAes256GcmSha384,
        MtlsCipherSuite::TlsChacha20Poly1305Sha256,
    };
    std::array<MtlsDnsName, max_peer_names> allowed_peer_dns{};
    std::array<MtlsSha256Fingerprint, max_peer_names> allowed_peer_pins{};
    std::uint8_t allowed_peer_count = 0;

    [[nodiscard]] constexpr std::expected<void, MtlsError>
    allow_peer_with_pin(MtlsDnsName name,
                        MtlsCertificateFingerprint fingerprint) noexcept {
        if (allowed_peer_count >= max_peer_names) {
            return std::unexpected(MtlsError::TooManyPeerNames);
        }
        allowed_peer_dns[allowed_peer_count] = name;
        allowed_peer_pins[allowed_peer_count] = fingerprint.value();
        ++allowed_peer_count;
        return {};
    }
};

struct MtlsConfig {
    MtlsCertificate ca_cert;
    MtlsCertificate client_cert;
    MtlsPrivateKey client_key;
    MtlsPolicy policy{};
};

using DeclaredMtlsConfig =
    safety::Tagged<MtlsConfig, safety::source::Mtls>;

struct MtlsPeerIdentity {
    MtlsDnsName dns_name{};
    MtlsCertificateFingerprint certificate_sha256{
        MtlsSha256Fingerprint{}};
    MtlsCipherSuite cipher = MtlsCipherSuite::TlsAes256GcmSha384;
};

using AuthenticatedMtlsPeer =
    safety::Tagged<MtlsPeerIdentity, safety::source::Mtls>;

class MtlsConnection {
public:
    MtlsConnection(MtlsConnection const&) = delete;
    MtlsConnection& operator=(MtlsConnection const&) = delete;
    MtlsConnection(MtlsConnection&&) = default;
    MtlsConnection& operator=(MtlsConnection&&) = default;

    [[nodiscard]] SocketFd socket() const noexcept { return socket_; }
    [[nodiscard]] AuthenticatedMtlsPeer const& authenticated_peer() const noexcept {
        return peer_;
    }

private:
    SocketFd socket_;
    AuthenticatedMtlsPeer peer_;

    constexpr MtlsConnection(SocketFd socket, AuthenticatedMtlsPeer peer) noexcept
        : socket_{socket}, peer_{peer} {}

    friend std::expected<MtlsConnection, MtlsError>
    connect_mtls(SocketFd, DeclaredMtlsConfig const&, MtlsDnsName,
                 MtlsCertificateFingerprint) noexcept;
};

[[nodiscard]] std::expected<MtlsCertificate, MtlsError>
admit_x509_certificate_pem(std::span<const std::byte> pem) noexcept;

template <MtlsKeyAlgorithm Algorithm>
    requires ApprovedMtlsKeyAlgorithm<Algorithm>
[[nodiscard]] std::expected<MtlsPrivateKey, MtlsError>
admit_private_key_pem(std::span<const std::byte> pem) noexcept {
    if (pem.empty()) {
        return std::unexpected(MtlsError::EmptyPrivateKey);
    }
    if (pem.size() > MtlsPrivateKeyBytes::max_bytes) {
        return std::unexpected(MtlsError::PrivateKeyTooLarge);
    }

    MtlsPrivateKeyBytes out{};
    for (std::size_t i = 0; i < pem.size(); ++i) {
        out.bytes[i] = pem[i];
    }
    out.nbytes = static_cast<std::uint16_t>(pem.size());
    out.algorithm = Algorithm;
    return std::expected<MtlsPrivateKey, MtlsError>{
        std::in_place, std::move(out)};
}

[[nodiscard]] constexpr std::expected<MtlsCertificateFingerprint, MtlsError>
admit_certificate_fingerprint(MtlsSha256Fingerprint fingerprint) noexcept {
    bool any = false;
    for (std::byte b : fingerprint.bytes) {
        any = any || b != std::byte{0};
    }
    if (!any) {
        return std::unexpected(MtlsError::EmptyFingerprint);
    }
    return MtlsCertificateFingerprint{fingerprint};
}

template <TlsVersion MinVersion = TlsVersion::V13,
          MtlsCipherSuite Primary = MtlsCipherSuite::TlsAes256GcmSha384,
          MtlsCipherSuite Secondary = MtlsCipherSuite::TlsChacha20Poly1305Sha256>
    requires SupportedMtlsVersion<MinVersion> &&
             ApprovedMtlsCipherSuite<Primary> &&
             ApprovedMtlsCipherSuite<Secondary>
[[nodiscard]] constexpr DeclaredMtlsConfig
mint_mtls_config(MtlsCertificate ca_cert,
                 MtlsCertificate client_cert,
                 MtlsPrivateKey client_key,
                 MtlsPolicy policy = {}) noexcept {
    policy.min_version = MinVersion;
    policy.max_version = TlsVersion::V13;
    policy.allowed_ciphers = MtlsCipherMask{Primary, Secondary};
    return DeclaredMtlsConfig{MtlsConfig{
        .ca_cert = std::move(ca_cert),
        .client_cert = std::move(client_cert),
        .client_key = std::move(client_key),
        .policy = policy,
    }};
}

[[nodiscard]] constexpr bool
mtls_cipher_is_approved(MtlsCipherSuite suite) noexcept {
    return suite == MtlsCipherSuite::TlsAes256GcmSha384 ||
           suite == MtlsCipherSuite::TlsChacha20Poly1305Sha256;
}

[[nodiscard]] constexpr bool
mtls_policy_allows_cipher(MtlsPolicy const& policy,
                          MtlsCipherSuite suite) noexcept {
    return mtls_cipher_is_approved(suite) && policy.allowed_ciphers.test(suite);
}

[[nodiscard]] constexpr bool
mtls_policy_allows_peer_name(MtlsPolicy const& policy,
                             MtlsDnsName peer) noexcept {
    for (std::uint8_t i = 0; i < policy.allowed_peer_count; ++i) {
        if (policy.allowed_peer_dns[i].view() == peer.view()) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] constexpr std::expected<void, MtlsError>
validate_mtls_policy(MtlsPolicy const& policy) noexcept {
    if (!policy.verify_peer) {
        return std::unexpected(MtlsError::PeerVerificationDisabled);
    }
    if (!policy.require_peer_cert) {
        return std::unexpected(MtlsError::PeerCertificateNotRequired);
    }
    if (policy.min_version != TlsVersion::V13 ||
        policy.max_version != TlsVersion::V13) {
        return std::unexpected(MtlsError::UnsupportedTlsVersion);
    }
    if (!mtls_policy_allows_cipher(
            policy, MtlsCipherSuite::TlsAes256GcmSha384) &&
        !mtls_policy_allows_cipher(
            policy, MtlsCipherSuite::TlsChacha20Poly1305Sha256)) {
        return std::unexpected(MtlsError::UnsupportedCipherSuite);
    }
    return {};
}

[[nodiscard]] constexpr std::expected<void, MtlsError>
validate_mtls_config(DeclaredMtlsConfig const& config) noexcept {
    auto const& raw = config.value();
    if (raw.ca_cert.peek().size == 0 || raw.client_cert.peek().size == 0) {
        return std::unexpected(MtlsError::EmptyCertificate);
    }
    if (raw.client_key.size() == 0) {
        return std::unexpected(MtlsError::EmptyPrivateKey);
    }
    return validate_mtls_policy(raw.policy);
}

[[nodiscard]] std::expected<AuthenticatedMtlsPeer, MtlsError>
admit_mtls_peer_from_handshake(DeclaredMtlsConfig const& config,
                               MtlsDnsName peer_dns,
                               MtlsCertificateFingerprint peer_fingerprint,
                               MtlsCipherSuite chosen_cipher) noexcept;

[[nodiscard]] std::expected<MtlsConnection, MtlsError>
connect_mtls(SocketFd socket,
             DeclaredMtlsConfig const& config,
             MtlsDnsName peer_dns,
             MtlsCertificateFingerprint peer_fingerprint) noexcept;

[[nodiscard]] std::expected<std::size_t, MtlsError>
mtls_send(MtlsConnection& connection, std::span<const std::byte> bytes) noexcept;

[[nodiscard]] std::expected<std::size_t, MtlsError>
mtls_recv(MtlsConnection& connection, std::span<std::byte> bytes) noexcept;

[[nodiscard]] std::expected<void, MtlsError>
enable_ktls_offload(MtlsConnection& connection, NicInterfaceName iface) noexcept;

static_assert(sizeof(MtlsCertificate) == sizeof(MtlsCertificateBytes));
static_assert(sizeof(MtlsPrivateKey) == sizeof(MtlsPrivateKeyBytes));
static_assert(sizeof(MtlsCertificateFingerprint) == sizeof(MtlsSha256Fingerprint));
static_assert(sizeof(AuthenticatedMtlsPeer) == sizeof(MtlsPeerIdentity));
static_assert(!std::copy_constructible<MtlsPrivateKeyBytes>);
static_assert(!std::copy_constructible<MtlsConfig>);
static_assert(std::move_constructible<MtlsConfig>);
static_assert(SupportedMtlsVersion<TlsVersion::V13>);
static_assert(!SupportedMtlsVersion<TlsVersion::V12>);
static_assert(ApprovedMtlsCipherSuite<MtlsCipherSuite::TlsAes256GcmSha384>);
static_assert(!ApprovedMtlsCipherSuite<MtlsCipherSuite::LegacyRsa3desSha>);
static_assert(ApprovedMtlsKeyAlgorithm<MtlsKeyAlgorithm::Ed25519>);
static_assert(!ApprovedMtlsKeyAlgorithm<MtlsKeyAlgorithm::RsaPkcs1>);

}  // namespace crucible::cntp
