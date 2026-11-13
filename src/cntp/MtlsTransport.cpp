#include <crucible/cntp/MtlsTransport.h>

#include <cstddef>
#include <utility>

namespace crucible::cntp {

::fixy::TextView mtls_error_name(MtlsError error) noexcept { return ::foundation::core::enum_name_text(error); }

::fixy::TextView tls_version_name(TlsVersion version) noexcept {
    switch (version) {
        case TlsVersion::V10:
            return "TLS1.0";
        case TlsVersion::V11:
            return "TLS1.1";
        case TlsVersion::V12:
            return "TLS1.2";
        case TlsVersion::V13:
            return "TLS1.3";
        default:
            return "<unknown TLS version>";
    }
}

::fixy::TextView mtls_cipher_suite_name(MtlsCipherSuite suite) noexcept {
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

::fixy::TextView mtls_key_algorithm_name(MtlsKeyAlgorithm algorithm) noexcept {
    switch (algorithm) {
        case MtlsKeyAlgorithm::Ed25519:
            return "ED25519";
        case MtlsKeyAlgorithm::EcdsaP256:
            return "ECDSA-P256";
        case MtlsKeyAlgorithm::RsaPkcs1:
            return "RSA-PKCS1";
        default:
            return "<unknown mTLS key algorithm>";
    }
}

::fixy::Result<MtlsCertificate, MtlsError> admit_x509_certificate_pem(::fixy::View<std::byte const> pem) noexcept {
    if (pem.size() == 0) {
        return ::fixy::err(MtlsError::EmptyCertificate);
    }
    if (pem.size() > MtlsCertificateBytes::max_bytes) {
        return ::fixy::err(MtlsError::CertificateTooLarge);
    }

    // A loop of bytes, and not a memmove: the library imports no memmove.
    MtlsCertificateBytes out{};
    ::fixy::View<std::byte> const target =
        out.bytes.view().window(0, pem.size()).expect("the certificate is at most max_bytes");
    for (auto [destination, source] :
         ::fixy::zip(target, pem).expect("a window of the size of the certificate has that size")) {
        *destination = *source;
    }
    out.size = static_cast<std::uint16_t>(pem.size());
    return ::fixy::mint_linear<MtlsCertificateBytes>(std::move(out));
}

namespace {

// A fingerprint is a hash of a public certificate, so the compare needs no
// constant time.
[[nodiscard]] bool same_fingerprint(MtlsSha256Fingerprint const& lhs, MtlsSha256Fingerprint const& rhs) noexcept {
    return ::fixy::equal(lhs.bytes.view(), rhs.bytes.view());
}

[[nodiscard]] bool has_fingerprint(MtlsSha256Fingerprint const& fingerprint) noexcept {
    return ::foundation::core::any_of(fingerprint.bytes.view(),
                                      [](std::byte value) noexcept { return value != std::byte{0}; });
}

}  // namespace

::fixy::Result<AuthenticatedMtlsPeer, MtlsError>
admit_mtls_peer_from_handshake(DeclaredMtlsConfig const& config, MtlsDnsName peer_dns,
                               MtlsCertificateFingerprint peer_fingerprint, MtlsCipherSuite chosen_cipher) noexcept {
    CRUCIBLE_TRY(_, validate_mtls_config(config));

    MtlsPolicy const& policy = config.value().policy;
    CRUCIBLE_TRY(slot, policy.find_peer(peer_dns).ok_or(MtlsError::PeerNameNotAllowed));
    if (!mtls_policy_allows_cipher(policy, chosen_cipher)) {
        return ::fixy::err(MtlsError::UnsupportedCipherSuite);
    }

    MtlsSha256Fingerprint const& pin = policy.allowed_peer_pins.at(slot);
    if (has_fingerprint(pin) && !same_fingerprint(pin, peer_fingerprint.value())) {
        return ::fixy::err(MtlsError::CertificatePinMismatch);
    }
    return ::fixy::mint_tagged<::fixy::tags::source::Mtls>(MtlsPeerIdentity{
        .dns_name = peer_dns,
        .certificate_sha256 = peer_fingerprint,
        .cipher = chosen_cipher,
    });
}

::fixy::Result<MtlsConnection, MtlsError> connect_mtls(SocketFd socket, DeclaredMtlsConfig const& config,
                                                       MtlsDnsName peer_dns,
                                                       MtlsCertificateFingerprint peer_fingerprint) noexcept {
    static_cast<void>(socket);
    CRUCIBLE_TRY(
        _, admit_mtls_peer_from_handshake(config, peer_dns, peer_fingerprint, MtlsCipherSuite::TlsAes256GcmSha384));
    return ::fixy::err(MtlsError::BackendUnavailable);
}

::fixy::Result<std::size_t, MtlsError> mtls_send(MtlsConnection& connection,
                                                 ::fixy::View<std::byte const> bytes) noexcept {
    static_cast<void>(connection);
    static_cast<void>(bytes);
    return ::fixy::err(MtlsError::BackendUnavailable);
}

::fixy::Result<std::size_t, MtlsError> mtls_recv(MtlsConnection& connection, ::fixy::View<std::byte> bytes) noexcept {
    static_cast<void>(connection);
    static_cast<void>(bytes);
    return ::fixy::err(MtlsError::BackendUnavailable);
}

::fixy::Result<::fixy::Unit, MtlsError> enable_ktls_offload(MtlsConnection& connection,
                                                            NicInterfaceName iface) noexcept {
    static_cast<void>(connection);
    static_cast<void>(iface);
    return ::fixy::err(MtlsError::KtlsOffloadDeferred);
}

}  // namespace crucible::cntp
