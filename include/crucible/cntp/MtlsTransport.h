#pragma once

#include <crucible/cntp/CongestionControl.h>
#include <crucible/cntp/Pacing.h>
#include <fixy/Bits.h>
#include <fixy/Core.h>
#include <fixy/Qtt.h>
#include <fixy/Secret.h>
#include <fixy/Tagged.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace crucible::cntp {

// False while the data plane is a stub.  connect_mtls, mtls_send and
// mtls_recv return MtlsError::BackendUnavailable, and enable_ktls_offload
// returns MtlsError::KtlsOffloadDeferred.  No handshake runs, no TLS library
// is bound and no kTLS socket option is installed, so nothing here
// authenticates a peer or encrypts a byte.  Everything above the data plane
// is typed admission of the facts such a backend must enforce.
inline constexpr bool data_plane_implemented = false;

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
    TlsAes256GcmSha384 = 1u << 0,
    TlsChacha20Poly1305Sha256 = 1u << 1,
    TlsAes128GcmSha256 = 1u << 2,
    LegacyRsa3desSha = 1u << 3,
};

enum class MtlsKeyAlgorithm : std::uint8_t {
    Ed25519,
    EcdsaP256,
    RsaPkcs1,
};

[[nodiscard]] ::fixy::TextView mtls_error_name(MtlsError error) noexcept;
[[nodiscard]] ::fixy::TextView tls_version_name(TlsVersion version) noexcept;
[[nodiscard]] ::fixy::TextView mtls_cipher_suite_name(MtlsCipherSuite suite) noexcept;
[[nodiscard]] ::fixy::TextView mtls_key_algorithm_name(MtlsKeyAlgorithm algorithm) noexcept;

template <TlsVersion Version>
concept SupportedMtlsVersion = Version == TlsVersion::V13;

template <MtlsCipherSuite Suite>
concept ApprovedMtlsCipherSuite =
    Suite == MtlsCipherSuite::TlsAes256GcmSha384 || Suite == MtlsCipherSuite::TlsChacha20Poly1305Sha256;

template <MtlsKeyAlgorithm Algorithm>
concept ApprovedMtlsKeyAlgorithm = Algorithm == MtlsKeyAlgorithm::Ed25519 || Algorithm == MtlsKeyAlgorithm::EcdsaP256;

using MtlsCipherMask = ::fixy::Bits<MtlsCipherSuite>;

struct MtlsCertificateBytes {
    static constexpr std::size_t max_bytes = 4096;

    ::foundation::core::FixedArray<std::byte, max_bytes> bytes{};
    std::uint16_t size = 0;

    // The first size bytes.  admit_x509_certificate_pem keeps the size at
    // most max_bytes, and a larger size ends the process.
    [[nodiscard]] constexpr ::fixy::View<std::byte const> view() const noexcept {
        return bytes.view().window(0, size).expect("the size of a certificate is at most max_bytes");
    }
};

struct MtlsPrivateKeyBytes {
    static constexpr std::size_t max_bytes = 4096;

    ::foundation::core::FixedArray<std::byte, max_bytes> bytes{};
    std::uint16_t nbytes = 0;
    MtlsKeyAlgorithm algorithm = MtlsKeyAlgorithm::Ed25519;

    constexpr MtlsPrivateKeyBytes() noexcept = default;
    MtlsPrivateKeyBytes(MtlsPrivateKeyBytes const&) = delete;
    MtlsPrivateKeyBytes& operator=(MtlsPrivateKeyBytes const&) = delete;

    constexpr MtlsPrivateKeyBytes(MtlsPrivateKeyBytes&& other) noexcept
        : bytes{other.bytes}, nbytes{other.nbytes}, algorithm{other.algorithm} {
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

    [[nodiscard]] constexpr std::size_t size_bytes() const noexcept { return nbytes; }

    [[nodiscard]] constexpr std::size_t size() const noexcept { return nbytes; }

    constexpr void zeroize() noexcept {
        for (auto& b : bytes) {
            b = std::byte{0};
        }
        nbytes = 0;
        algorithm = MtlsKeyAlgorithm::Ed25519;
    }
};

// The DNS name of a peer, with at most max_bytes characters.  from() checks
// each character and is the one writer, so each name holds only the
// characters of a DNS name.
class MtlsDnsName {
public:
    static constexpr std::size_t max_bytes = 253;

    constexpr MtlsDnsName() noexcept = default;

    // The characters of the name.  The view borrows this name, so a view of a
    // temporary name is refused.
    [[nodiscard]] constexpr ::fixy::TextView view() const& noexcept { return characters_.view(); }
    ::fixy::TextView
    view() const&& = delete("a view of a temporary MtlsDnsName points into an object that the full expression ends");

    // A Result<MtlsDnsName, MtlsError>.  The type of a Result needs a complete
    // name, so the definition stands after the class.
    [[nodiscard]] static constexpr auto from(::fixy::TextView name) noexcept;

private:
    ::fixy::FixedText<max_bytes> characters_{};
};

// A name of more than max_bytes characters does not fit, and append()
// refuses it with one compare.  Then each character takes one check.
constexpr auto MtlsDnsName::from(::fixy::TextView name) noexcept {
    using Admitted = ::fixy::Result<MtlsDnsName, MtlsError>;
    if (name.size() == 0) {
        return Admitted{::fixy::err(MtlsError::EmptyPeerName)};
    }
    MtlsDnsName out{};
    if (out.characters_.append(name).is_err()) {
        return Admitted{::fixy::err(MtlsError::InvalidPeerName)};
    }
    if (::fixy::starts_with(name, ".") || ::fixy::ends_with(name, ".") || ::fixy::starts_with(name, "-")
        || ::fixy::ends_with(name, "-")) {
        return Admitted{::fixy::err(MtlsError::InvalidPeerName)};
    }

    bool previous_dot = false;
    for (char const character : name) {
        bool const is_dot = character == '.';
        bool const is_dns_character = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z')
                                   || (character >= '0' && character <= '9') || character == '-' || is_dot;
        if (!is_dns_character || (is_dot && previous_dot)) {
            return Admitted{::fixy::err(MtlsError::InvalidPeerName)};
        }
        previous_dot = is_dot;
    }
    return Admitted{out};
}

struct MtlsSha256Fingerprint {
    static constexpr std::size_t bytes_count = 32;

    ::foundation::core::FixedArray<std::byte, bytes_count> bytes{};
};

using MtlsCertificate = ::fixy::Linear<MtlsCertificateBytes>;
using MtlsPrivateKey = ::fixy::Secret<MtlsPrivateKeyBytes>;
using MtlsCertificateFingerprint = ::fixy::Tagged<MtlsSha256Fingerprint, ::fixy::tags::source::Mtls>;

struct MtlsPolicy;

// The count of the allowed peers of a policy.  MtlsPolicy::allow_peer_with_pin
// is its one writer, and it refuses a peer past max_peer_names.  So the
// count is at most the extent of the two peer arrays.
class MtlsPeerCount {
public:
    constexpr MtlsPeerCount() noexcept = default;

    [[nodiscard]] constexpr std::uint8_t value() const noexcept { return value_; }

private:
    std::uint8_t value_ = 0;

    friend struct MtlsPolicy;
};

struct MtlsPolicy {
    static constexpr std::size_t max_peer_names = 8;
    using PeerIndex = ::fixy::Index<max_peer_names>;

    bool verify_peer = true;
    bool require_peer_cert = true;
    TlsVersion min_version = TlsVersion::V13;
    TlsVersion max_version = TlsVersion::V13;
    MtlsCipherMask allowed_ciphers{
        MtlsCipherSuite::TlsAes256GcmSha384,
        MtlsCipherSuite::TlsChacha20Poly1305Sha256,
    };
    ::foundation::core::FixedArray<MtlsDnsName, max_peer_names> allowed_peer_dns{};
    ::foundation::core::FixedArray<MtlsSha256Fingerprint, max_peer_names> allowed_peer_pins{};
    MtlsPeerCount allowed_peer_count{};

    // The one writer of allowed_peer_count.  It puts the peer in the next
    // free slot, or refuses the peer when no slot is free.
    [[nodiscard]] constexpr ::fixy::Result<::fixy::Unit, MtlsError>
    allow_peer_with_pin(MtlsDnsName name, MtlsCertificateFingerprint fingerprint) noexcept {
        CRUCIBLE_TRY(slot, PeerIndex::check(allowed_peer_count.value()).ok_or(MtlsError::TooManyPeerNames));
        allowed_peer_dns.at(slot) = name;
        allowed_peer_pins.at(slot) = fingerprint.value();
        ++allowed_peer_count.value_;
        return ::fixy::Unit{};
    }

    // The slot of the allowed peer with this name, or no value.  The loop
    // reads only the slots that allow_peer_with_pin wrote.  Complexity:
    // O(max_peer_names) name compares.
    [[nodiscard]] constexpr ::fixy::Option<PeerIndex> find_peer(MtlsDnsName const& name) const noexcept {
        for (PeerIndex index : allowed_peer_dns.indices()) {
            if (index.raw() >= allowed_peer_count.value()) {
                break;
            }
            if (allowed_peer_dns.at(index).view() == name.view()) {
                return ::fixy::some(index);
            }
        }
        return ::fixy::none;
    }
};

struct MtlsConfig {
    MtlsCertificate ca_cert;
    MtlsCertificate client_cert;
    MtlsPrivateKey client_key;
    MtlsPolicy policy{};
};

using DeclaredMtlsConfig = ::fixy::Tagged<MtlsConfig, ::fixy::tags::source::Mtls>;

struct MtlsPeerIdentity {
    MtlsDnsName dns_name{};
    MtlsCertificateFingerprint certificate_sha256 =
        ::fixy::mint_tagged<::fixy::tags::source::Mtls>(MtlsSha256Fingerprint{});
    MtlsCipherSuite cipher = MtlsCipherSuite::TlsAes256GcmSha384;
};

using AuthenticatedMtlsPeer = ::fixy::Tagged<MtlsPeerIdentity, ::fixy::tags::source::Mtls>;

class MtlsConnectionDoor;

class MtlsConnection {
public:
    MtlsConnection(MtlsConnection const&) = delete;
    MtlsConnection& operator=(MtlsConnection const&) = delete;
    MtlsConnection(MtlsConnection&&) = default;
    MtlsConnection& operator=(MtlsConnection&&) = default;

    [[nodiscard]] SocketFd socket() const noexcept { return socket_; }
    [[nodiscard]] AuthenticatedMtlsPeer const& authenticated_peer() const noexcept { return peer_; }

private:
    SocketFd socket_;
    AuthenticatedMtlsPeer peer_;

    constexpr MtlsConnection(SocketFd socket, AuthenticatedMtlsPeer peer) noexcept : socket_{socket}, peer_{peer} {}

    friend class MtlsConnectionDoor;
};

// The constructor of a connection.  Its one member is private, and
// connect_mtls is its one friend.  The friend names the Result of a
// connection, so it stands after the connection is complete.
class MtlsConnectionDoor {
    [[nodiscard]] static constexpr MtlsConnection build(SocketFd socket, AuthenticatedMtlsPeer peer) noexcept {
        return MtlsConnection{socket, peer};
    }

    friend ::fixy::Result<MtlsConnection, MtlsError> connect_mtls(SocketFd, DeclaredMtlsConfig const&, MtlsDnsName,
                                                                  MtlsCertificateFingerprint) noexcept;
};

[[nodiscard]] ::fixy::Result<MtlsCertificate, MtlsError>
admit_x509_certificate_pem(::fixy::View<std::byte const> pem) noexcept;

template <MtlsKeyAlgorithm Algorithm>
    requires ApprovedMtlsKeyAlgorithm<Algorithm>
[[nodiscard]] ::fixy::Result<MtlsPrivateKey, MtlsError>
admit_private_key_pem(::fixy::View<std::byte const> pem) noexcept {
    if (pem.size() == 0) {
        return ::fixy::err(MtlsError::EmptyPrivateKey);
    }
    if (pem.size() > MtlsPrivateKeyBytes::max_bytes) {
        return ::fixy::err(MtlsError::PrivateKeyTooLarge);
    }

    MtlsPrivateKeyBytes out{};
    ::fixy::copy(out.bytes.view().window(0, pem.size()).expect("the key is at most max_bytes"), pem)
        .expect("a window of the size of the key has that size");
    out.nbytes = static_cast<std::uint16_t>(pem.size());
    out.algorithm = Algorithm;
    return ::fixy::mint_secret<MtlsPrivateKeyBytes>(std::move(out));
}

[[nodiscard]] constexpr ::fixy::Result<MtlsCertificateFingerprint, MtlsError>
admit_certificate_fingerprint(MtlsSha256Fingerprint fingerprint) noexcept {
    bool any = false;
    for (std::byte b : fingerprint.bytes) {
        any = any || b != std::byte{0};
    }
    if (!any) {
        return ::fixy::err(MtlsError::EmptyFingerprint);
    }
    return ::fixy::mint_tagged<::fixy::tags::source::Mtls>(fingerprint);
}

template <TlsVersion MinVersion = TlsVersion::V13, MtlsCipherSuite Primary = MtlsCipherSuite::TlsAes256GcmSha384,
          MtlsCipherSuite Secondary = MtlsCipherSuite::TlsChacha20Poly1305Sha256>
    requires SupportedMtlsVersion<MinVersion> && ApprovedMtlsCipherSuite<Primary> && ApprovedMtlsCipherSuite<Secondary>
[[nodiscard]] constexpr DeclaredMtlsConfig mint_mtls_config(MtlsCertificate ca_cert, MtlsCertificate client_cert,
                                                            MtlsPrivateKey client_key,
                                                            MtlsPolicy policy = {}) noexcept {
    policy.min_version = MinVersion;
    policy.max_version = TlsVersion::V13;
    policy.allowed_ciphers = MtlsCipherMask{Primary, Secondary};
    return ::fixy::mint_tagged<::fixy::tags::source::Mtls>(MtlsConfig{
        .ca_cert = std::move(ca_cert),
        .client_cert = std::move(client_cert),
        .client_key = std::move(client_key),
        .policy = policy,
    });
}

[[nodiscard]] constexpr bool mtls_cipher_is_approved(MtlsCipherSuite suite) noexcept {
    return suite == MtlsCipherSuite::TlsAes256GcmSha384 || suite == MtlsCipherSuite::TlsChacha20Poly1305Sha256;
}

[[nodiscard]] constexpr bool mtls_policy_allows_cipher(MtlsPolicy const& policy, MtlsCipherSuite suite) noexcept {
    return mtls_cipher_is_approved(suite) && policy.allowed_ciphers.test(suite);
}

[[nodiscard]] constexpr bool mtls_policy_allows_peer_name(MtlsPolicy const& policy, MtlsDnsName peer) noexcept {
    return policy.find_peer(peer).is_some();
}

[[nodiscard]] constexpr ::fixy::Result<::fixy::Unit, MtlsError>
validate_mtls_policy(MtlsPolicy const& policy) noexcept {
    if (!policy.verify_peer) {
        return ::fixy::err(MtlsError::PeerVerificationDisabled);
    }
    if (!policy.require_peer_cert) {
        return ::fixy::err(MtlsError::PeerCertificateNotRequired);
    }
    if (policy.min_version != TlsVersion::V13 || policy.max_version != TlsVersion::V13) {
        return ::fixy::err(MtlsError::UnsupportedTlsVersion);
    }
    if (!mtls_policy_allows_cipher(policy, MtlsCipherSuite::TlsAes256GcmSha384)
        && !mtls_policy_allows_cipher(policy, MtlsCipherSuite::TlsChacha20Poly1305Sha256)) {
        return ::fixy::err(MtlsError::UnsupportedCipherSuite);
    }
    return ::fixy::Unit{};
}

[[nodiscard]] constexpr ::fixy::Result<::fixy::Unit, MtlsError>
validate_mtls_config(DeclaredMtlsConfig const& config) noexcept {
    auto const& raw = config.value();
    if (raw.ca_cert.peek().size == 0 || raw.client_cert.peek().size == 0) {
        return ::fixy::err(MtlsError::EmptyCertificate);
    }
    if (raw.client_key.declassify<::fixy::tags::secret_policy::LengthOnly>() == 0) {
        return ::fixy::err(MtlsError::EmptyPrivateKey);
    }
    return validate_mtls_policy(raw.policy);
}

[[nodiscard]] ::fixy::Result<AuthenticatedMtlsPeer, MtlsError>
admit_mtls_peer_from_handshake(DeclaredMtlsConfig const& config, MtlsDnsName peer_dns,
                               MtlsCertificateFingerprint peer_fingerprint, MtlsCipherSuite chosen_cipher) noexcept;

// The four entrypoints below carry [[deprecated]] not because they are going
// away but because the attribute makes every call site warn, so a stub cannot
// be reached without notice at compile time.
[[nodiscard, deprecated("CRUCIBLE_STUB: no TLS 1.3 handshake is performed; "
                        "returns MtlsError::BackendUnavailable")]]
::fixy::Result<MtlsConnection, MtlsError> connect_mtls(SocketFd socket, DeclaredMtlsConfig const& config,
                                                       MtlsDnsName peer_dns,
                                                       MtlsCertificateFingerprint peer_fingerprint) noexcept;

[[nodiscard, deprecated("CRUCIBLE_STUB: there is no record layer to send on; "
                        "returns MtlsError::BackendUnavailable")]]
::fixy::Result<std::size_t, MtlsError> mtls_send(MtlsConnection& connection,
                                                 ::fixy::View<std::byte const> bytes) noexcept;

[[nodiscard, deprecated("CRUCIBLE_STUB: there is no record layer to receive on; "
                        "returns MtlsError::BackendUnavailable")]]
::fixy::Result<std::size_t, MtlsError> mtls_recv(MtlsConnection& connection, ::fixy::View<std::byte> bytes) noexcept;

[[nodiscard, deprecated("CRUCIBLE_STUB: no TLS_TX/TLS_RX socket option is installed; "
                        "returns MtlsError::KtlsOffloadDeferred")]]
::fixy::Result<::fixy::Unit, MtlsError> enable_ktls_offload(MtlsConnection& connection,
                                                            NicInterfaceName iface) noexcept;

}  // namespace crucible::cntp
