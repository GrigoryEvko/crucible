#include <crucible/cntp/MtlsTransport.h>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <span>
#include <string_view>
#include <type_traits>

namespace cntp = crucible::cntp;
namespace saf = crucible::safety;

namespace {

[[nodiscard]] std::array<std::byte, 32> fingerprint_bytes(std::byte seed) {
    std::array<std::byte, 32> out{};
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = static_cast<std::byte>(
            std::to_integer<unsigned>(seed) + static_cast<unsigned>(i + 1u));
    }
    return out;
}

[[nodiscard]] std::array<std::byte, 96> material(std::byte seed) {
    std::array<std::byte, 96> out{};
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = static_cast<std::byte>(
            std::to_integer<unsigned>(seed) ^ static_cast<unsigned>(i * 13u));
    }
    return out;
}

void test_name_and_error_surfaces() {
    assert(cntp::mtls_error_name(cntp::MtlsError::BackendUnavailable) ==
           std::string_view{"BackendUnavailable"});
    assert(cntp::tls_version_name(cntp::TlsVersion::V13) ==
           std::string_view{"TLS1.3"});
    assert(cntp::mtls_cipher_suite_name(
               cntp::MtlsCipherSuite::TlsAes256GcmSha384) ==
           std::string_view{"TLS_AES_256_GCM_SHA384"});
    assert(cntp::mtls_key_algorithm_name(cntp::MtlsKeyAlgorithm::Ed25519) ==
           std::string_view{"ED25519"});

    auto dns = cntp::MtlsDnsName::from("relay-a.example.org");
    assert(dns.has_value());
    assert(dns->view() == "relay-a.example.org");

    auto empty = cntp::MtlsDnsName::from("");
    assert(!empty.has_value());
    assert(empty.error() == cntp::MtlsError::EmptyPeerName);

    auto bad = cntp::MtlsDnsName::from("../relay");
    assert(!bad.has_value());
    assert(bad.error() == cntp::MtlsError::InvalidPeerName);

    std::printf("  test_name_and_error_surfaces: PASSED\n");
}

void test_material_admission() {
    auto ca_bytes = material(std::byte{0x11});
    auto cert = cntp::admit_x509_certificate_pem(ca_bytes);
    assert(cert.has_value());
    assert(cert->peek().size == ca_bytes.size());
    assert(cert->peek().view()[0] == ca_bytes[0]);

    std::array<std::byte, 0> empty{};
    auto empty_cert = cntp::admit_x509_certificate_pem(empty);
    assert(!empty_cert.has_value());
    assert(empty_cert.error() == cntp::MtlsError::EmptyCertificate);

    auto key_bytes = material(std::byte{0x44});
    auto key = cntp::admit_private_key_pem<cntp::MtlsKeyAlgorithm::Ed25519>(
        key_bytes);
    assert(key.has_value());
    assert(key->size() == key_bytes.size());

    cntp::MtlsSha256Fingerprint raw_fp{fingerprint_bytes(std::byte{0x70})};
    auto fp = cntp::admit_certificate_fingerprint(raw_fp);
    assert(fp.has_value());

    cntp::MtlsSha256Fingerprint zero_fp{};
    auto zero = cntp::admit_certificate_fingerprint(zero_fp);
    assert(!zero.has_value());
    assert(zero.error() == cntp::MtlsError::EmptyFingerprint);

    std::printf("  test_material_admission:       PASSED\n");
}

void test_policy_and_peer_admission() {
    auto ca_bytes = material(std::byte{0x21});
    auto cert_bytes = material(std::byte{0x22});
    auto key_bytes = material(std::byte{0x23});

    auto ca = cntp::admit_x509_certificate_pem(ca_bytes);
    auto cert = cntp::admit_x509_certificate_pem(cert_bytes);
    auto key = cntp::admit_private_key_pem<cntp::MtlsKeyAlgorithm::EcdsaP256>(
        key_bytes);
    assert(ca.has_value());
    assert(cert.has_value());
    assert(key.has_value());

    auto allowed = cntp::MtlsDnsName::from("federation-a.example.org");
    auto denied = cntp::MtlsDnsName::from("federation-b.example.org");
    assert(allowed.has_value());
    assert(denied.has_value());

    cntp::MtlsSha256Fingerprint raw_fp{fingerprint_bytes(std::byte{0x55})};
    auto fp = cntp::admit_certificate_fingerprint(raw_fp);
    assert(fp.has_value());

    cntp::MtlsPolicy policy{};
    assert(policy.allow_peer_with_pin(*allowed, *fp).has_value());

    auto config = cntp::mint_mtls_config(
        std::move(*ca), std::move(*cert), std::move(*key), policy);
    static_assert(std::same_as<
                  std::remove_cvref_t<decltype(config)>,
                  cntp::DeclaredMtlsConfig>);
    static_assert(std::same_as<
                  cntp::DeclaredMtlsConfig::tag_type,
                  saf::source::Mtls>);

    auto valid = cntp::validate_mtls_config(config);
    assert(valid.has_value());

    auto peer = cntp::admit_mtls_peer_from_handshake(
        config,
        *allowed,
        *fp,
        cntp::MtlsCipherSuite::TlsAes256GcmSha384);
    assert(peer.has_value());
    assert(peer->value().dns_name.view() == allowed->view());
    assert(peer->value().cipher ==
           cntp::MtlsCipherSuite::TlsAes256GcmSha384);

    auto wrong_name = cntp::admit_mtls_peer_from_handshake(
        config,
        *denied,
        *fp,
        cntp::MtlsCipherSuite::TlsAes256GcmSha384);
    assert(!wrong_name.has_value());
    assert(wrong_name.error() == cntp::MtlsError::PeerNameNotAllowed);

    auto wrong_cipher = cntp::admit_mtls_peer_from_handshake(
        config,
        *allowed,
        *fp,
        cntp::MtlsCipherSuite::TlsAes128GcmSha256);
    assert(!wrong_cipher.has_value());
    assert(wrong_cipher.error() == cntp::MtlsError::UnsupportedCipherSuite);

    cntp::MtlsSha256Fingerprint other_raw_fp{
        fingerprint_bytes(std::byte{0x60})};
    auto other_fp = cntp::admit_certificate_fingerprint(other_raw_fp);
    assert(other_fp.has_value());
    auto wrong_pin = cntp::admit_mtls_peer_from_handshake(
        config,
        *allowed,
        *other_fp,
        cntp::MtlsCipherSuite::TlsAes256GcmSha384);
    assert(!wrong_pin.has_value());
    assert(wrong_pin.error() == cntp::MtlsError::CertificatePinMismatch);

    auto fd = cntp::admit_socket_fd(3);
    assert(fd.has_value());
    auto connection = cntp::connect_mtls(*fd, config, *allowed, *fp);
    assert(!connection.has_value());
    assert(connection.error() == cntp::MtlsError::BackendUnavailable);

    std::printf("  test_policy_and_peer_admission: PASSED\n");
}

void test_reject_insecure_policy() {
    cntp::MtlsPolicy policy{};
    policy.verify_peer = false;
    auto no_verify = cntp::validate_mtls_policy(policy);
    assert(!no_verify.has_value());
    assert(no_verify.error() == cntp::MtlsError::PeerVerificationDisabled);

    policy.verify_peer = true;
    policy.require_peer_cert = false;
    auto no_peer_cert = cntp::validate_mtls_policy(policy);
    assert(!no_peer_cert.has_value());
    assert(no_peer_cert.error() == cntp::MtlsError::PeerCertificateNotRequired);

    policy.require_peer_cert = true;
    policy.min_version = cntp::TlsVersion::V12;
    auto old_tls = cntp::validate_mtls_policy(policy);
    assert(!old_tls.has_value());
    assert(old_tls.error() == cntp::MtlsError::UnsupportedTlsVersion);

    auto ca_bytes = material(std::byte{0x31});
    auto cert_bytes = material(std::byte{0x32});
    auto ca = cntp::admit_x509_certificate_pem(ca_bytes);
    auto cert = cntp::admit_x509_certificate_pem(cert_bytes);
    assert(ca.has_value());
    assert(cert.has_value());
    auto empty_key_config = cntp::mint_mtls_config(
        std::move(*ca),
        std::move(*cert),
        cntp::MtlsPrivateKey{cntp::MtlsPrivateKeyBytes{}});
    auto empty_key = cntp::validate_mtls_config(empty_key_config);
    assert(!empty_key.has_value());
    assert(empty_key.error() == cntp::MtlsError::EmptyPrivateKey);

    std::printf("  test_reject_insecure_policy:   PASSED\n");
}

}  // namespace

int main() {
    static_assert(sizeof(cntp::MtlsCertificate) ==
                  sizeof(cntp::MtlsCertificateBytes));
    static_assert(sizeof(cntp::MtlsPrivateKey) ==
                  sizeof(cntp::MtlsPrivateKeyBytes));
    static_assert(sizeof(cntp::AuthenticatedMtlsPeer) ==
                  sizeof(cntp::MtlsPeerIdentity));
    static_assert(!std::copy_constructible<cntp::MtlsPrivateKeyBytes>);
    static_assert(!std::copy_constructible<cntp::MtlsConfig>);
    static_assert(std::move_constructible<cntp::MtlsConfig>);
    static_assert(cntp::SupportedMtlsVersion<cntp::TlsVersion::V13>);
    static_assert(!cntp::SupportedMtlsVersion<cntp::TlsVersion::V12>);
    static_assert(cntp::ApprovedMtlsCipherSuite<
                  cntp::MtlsCipherSuite::TlsAes256GcmSha384>);
    static_assert(!cntp::ApprovedMtlsCipherSuite<
                  cntp::MtlsCipherSuite::LegacyRsa3desSha>);
    static_assert(cntp::ApprovedMtlsKeyAlgorithm<
                  cntp::MtlsKeyAlgorithm::Ed25519>);
    static_assert(!cntp::ApprovedMtlsKeyAlgorithm<
                  cntp::MtlsKeyAlgorithm::RsaPkcs1>);

    std::printf("test_cntp_mtls_transport:\n");
    test_name_and_error_surfaces();
    test_material_admission();
    test_policy_and_peer_admission();
    test_reject_insecure_policy();
    std::printf("test_cntp_mtls_transport: all PASSED\n");
    return 0;
}
