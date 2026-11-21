#include <crucible/cntp/MtlsTransport.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <string_view>
#include <type_traits>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

// Every data-plane entry point this file calls carries a
// [[deprecated("CRUCIBLE_STUB:...")]] attribute, because no live
// backend stands behind it yet.  Calling them on purpose is the whole
// point here, so the warning is suppressed for this file alone.  The
// bodies below pin the return codes that show the entry points are
// still stubs.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

namespace cntp = crucible::cntp;

namespace {

using FingerprintBytes = ::foundation::core::FixedArray<std::byte, cntp::MtlsSha256Fingerprint::bytes_count>;
using MaterialBytes = ::foundation::core::FixedArray<std::byte, 96>;

// The bytes of a fingerprint: seed + 1, seed + 2, and so on.
[[nodiscard]] FingerprintBytes make_fingerprint_bytes(unsigned seed) {
    FingerprintBytes out{};
    for (auto index : out.indices()) {
        std::byte const value = static_cast<std::byte>(seed + index.raw() + 1u);
        out.with_element(index, [value](std::byte& byte) noexcept { byte = value; });
    }
    return out;
}

// The bytes of a certificate or a key that the test admits.
[[nodiscard]] MaterialBytes make_material(unsigned seed) {
    MaterialBytes out{};
    for (auto index : out.indices()) {
        std::byte const value = static_cast<std::byte>(seed ^ (index.raw() * 13u));
        out.with_element(index, [value](std::byte& byte) noexcept { byte = value; });
    }
    return out;
}

// The value of a call that succeeds in this test.
template <class Value>
Value take(::fixy::Result<Value, cntp::MtlsError> result) {
    return std::move(result).expect("the call succeeds in this test");
}

// The error of a certificate admission.  A certificate is linear, so the
// certificate of an admission that succeeds ends here, and the error then
// holds no value.
[[nodiscard]] ::fixy::Option<cntp::MtlsError>
error_of(::fixy::Result<cntp::MtlsCertificate, cntp::MtlsError> admitted) {
    return std::move(admitted).match(
        [](cntp::MtlsCertificate cert) noexcept {
            drop(std::move(cert));
            return ::fixy::Option<cntp::MtlsError>{::fixy::none};
        },
        [](cntp::MtlsError error) noexcept { return ::fixy::some(error); });
}

// An admitted fingerprint of the bytes of make_fingerprint_bytes(seed).
[[nodiscard]] cntp::MtlsCertificateFingerprint make_pin(unsigned seed) {
    return take(cntp::admit_certificate_fingerprint(cntp::MtlsSha256Fingerprint{make_fingerprint_bytes(seed)}));
}

void test_name_and_error_surfaces() {
    CRUCIBLE_TEST_ASSERT(kTest, cntp::mtls_error_name(cntp::MtlsError::BackendUnavailable) == "BackendUnavailable");
    CRUCIBLE_TEST_ASSERT(kTest, cntp::tls_version_name(cntp::TlsVersion::V13) == "TLS1.3");
    CRUCIBLE_TEST_ASSERT(kTest, cntp::mtls_cipher_suite_name(cntp::MtlsCipherSuite::TlsAes256GcmSha384)
                                    == "TLS_AES_256_GCM_SHA384");
    CRUCIBLE_TEST_ASSERT(kTest, cntp::mtls_key_algorithm_name(cntp::MtlsKeyAlgorithm::Ed25519) == "ED25519");

    cntp::MtlsDnsName const dns = take(cntp::MtlsDnsName::from("relay-a.example.org"));
    CRUCIBLE_TEST_ASSERT(kTest, dns.view() == "relay-a.example.org");

    CRUCIBLE_TEST_ASSERT(kTest, cntp::MtlsDnsName::from("").err() == ::fixy::some(cntp::MtlsError::EmptyPeerName));
    CRUCIBLE_TEST_ASSERT(kTest,
                         cntp::MtlsDnsName::from("../relay").err() == ::fixy::some(cntp::MtlsError::InvalidPeerName));
    CRUCIBLE_TEST_ASSERT(kTest, cntp::MtlsDnsName::from("relay..example.org").err()
                                    == ::fixy::some(cntp::MtlsError::InvalidPeerName));
    CRUCIBLE_TEST_ASSERT(kTest, cntp::MtlsDnsName::from("relay.example.org-").err()
                                    == ::fixy::some(cntp::MtlsError::InvalidPeerName));

    // A name of max_bytes characters fits, and one more character does not.
    ::fixy::FixedText<cntp::MtlsDnsName::max_bytes + 1> long_name{};
    for (std::size_t count = 0; count < cntp::MtlsDnsName::max_bytes; ++count) {
        CRUCIBLE_FATAL_INVARIANT(long_name.append('a').is_ok());
    }
    CRUCIBLE_TEST_ASSERT(kTest, cntp::MtlsDnsName::from(long_name.view()).is_ok());
    CRUCIBLE_FATAL_INVARIANT(long_name.append('a').is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, cntp::MtlsDnsName::from(long_name.view()).err()
                                    == ::fixy::some(cntp::MtlsError::InvalidPeerName));

    ::fixy::check::pass(kTest, "  test_name_and_error_surfaces: PASSED\n");
}

void test_material_admission() {
    MaterialBytes const ca_bytes = make_material(0x11);
    cntp::MtlsCertificate cert = take(cntp::admit_x509_certificate_pem(ca_bytes.view()));
    CRUCIBLE_TEST_ASSERT(kTest, cert.peek().size == ca_bytes.size());
    CRUCIBLE_TEST_ASSERT(kTest, ::fixy::equal(cert.peek().view(), ca_bytes.view()));
    drop(std::move(cert));

    CRUCIBLE_TEST_ASSERT(kTest, error_of(cntp::admit_x509_certificate_pem(::fixy::View<std::byte const>{}))
                                    == ::fixy::some(cntp::MtlsError::EmptyCertificate));

    // The certificate and the key refuse one byte past their bound.
    ::foundation::core::FixedArray<std::byte, cntp::MtlsCertificateBytes::max_bytes + 1> const large{};
    CRUCIBLE_TEST_ASSERT(kTest, error_of(cntp::admit_x509_certificate_pem(large.view()))
                                    == ::fixy::some(cntp::MtlsError::CertificateTooLarge));
    CRUCIBLE_TEST_ASSERT(kTest, cntp::admit_private_key_pem<cntp::MtlsKeyAlgorithm::Ed25519>(large.view()).err()
                                    == ::fixy::some(cntp::MtlsError::PrivateKeyTooLarge));

    MaterialBytes const key_bytes = make_material(0x44);
    cntp::MtlsPrivateKey const key =
        take(cntp::admit_private_key_pem<cntp::MtlsKeyAlgorithm::Ed25519>(key_bytes.view()));
    CRUCIBLE_TEST_ASSERT(kTest, key.declassify<::fixy::tags::secret_policy::LengthOnly>() == key_bytes.size());
    CRUCIBLE_TEST_ASSERT(
        kTest, cntp::admit_private_key_pem<cntp::MtlsKeyAlgorithm::Ed25519>(::fixy::View<std::byte const>{}).err()
                   == ::fixy::some(cntp::MtlsError::EmptyPrivateKey));

    CRUCIBLE_TEST_ASSERT(
        kTest, cntp::admit_certificate_fingerprint(cntp::MtlsSha256Fingerprint{make_fingerprint_bytes(0x70)}).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, cntp::admit_certificate_fingerprint(cntp::MtlsSha256Fingerprint{}).err()
                                    == ::fixy::some(cntp::MtlsError::EmptyFingerprint));

    ::fixy::check::pass(kTest, "  test_material_admission:       PASSED\n");
}

void test_policy_and_peer_admission() {
    MaterialBytes const ca_bytes = make_material(0x21);
    MaterialBytes const cert_bytes = make_material(0x22);
    MaterialBytes const key_bytes = make_material(0x23);

    cntp::MtlsCertificate ca = take(cntp::admit_x509_certificate_pem(ca_bytes.view()));
    cntp::MtlsCertificate cert = take(cntp::admit_x509_certificate_pem(cert_bytes.view()));
    cntp::MtlsPrivateKey key = take(cntp::admit_private_key_pem<cntp::MtlsKeyAlgorithm::EcdsaP256>(key_bytes.view()));

    cntp::MtlsDnsName const allowed = take(cntp::MtlsDnsName::from("federation-a.example.org"));
    cntp::MtlsDnsName const denied = take(cntp::MtlsDnsName::from("federation-b.example.org"));
    cntp::MtlsCertificateFingerprint const pin = make_pin(0x55);

    cntp::MtlsPolicy policy{};
    CRUCIBLE_TEST_ASSERT(kTest, policy.allow_peer_with_pin(allowed, pin).is_ok());

    auto config = cntp::mint_mtls_config(std::move(ca), std::move(cert), std::move(key), policy);
    static_assert(std::same_as<std::remove_cvref_t<decltype(config)>, cntp::DeclaredMtlsConfig>);
    static_assert(std::same_as<cntp::DeclaredMtlsConfig::tag_type, ::fixy::tags::source::Mtls>);

    CRUCIBLE_TEST_ASSERT(kTest, cntp::validate_mtls_config(config).is_ok());

    cntp::AuthenticatedMtlsPeer const peer =
        take(cntp::admit_mtls_peer_from_handshake(config, allowed, pin, cntp::MtlsCipherSuite::TlsAes256GcmSha384));
    CRUCIBLE_TEST_ASSERT(kTest, peer.value().dns_name.view() == allowed.view());
    CRUCIBLE_TEST_ASSERT(kTest, peer.value().cipher == cntp::MtlsCipherSuite::TlsAes256GcmSha384);

    CRUCIBLE_TEST_ASSERT(
        kTest,
        cntp::admit_mtls_peer_from_handshake(config, denied, pin, cntp::MtlsCipherSuite::TlsAes256GcmSha384).err()
            == ::fixy::some(cntp::MtlsError::PeerNameNotAllowed));
    CRUCIBLE_TEST_ASSERT(
        kTest,
        cntp::admit_mtls_peer_from_handshake(config, allowed, pin, cntp::MtlsCipherSuite::TlsAes128GcmSha256).err()
            == ::fixy::some(cntp::MtlsError::UnsupportedCipherSuite));
    CRUCIBLE_TEST_ASSERT(kTest, cntp::admit_mtls_peer_from_handshake(config, allowed, make_pin(0x60),
                                                                     cntp::MtlsCipherSuite::TlsAes256GcmSha384)
                                        .err()
                                    == ::fixy::some(cntp::MtlsError::CertificatePinMismatch));

    // The stub refuses each connect before it touches the socket, so the
    // empty ref serves.
    cntp::SocketFd const fd{};
    CRUCIBLE_TEST_ASSERT(kTest, cntp::connect_mtls(fd, config, allowed, pin).err()
                                    == ::fixy::some(cntp::MtlsError::BackendUnavailable));

    ::fixy::check::pass(kTest, "  test_policy_and_peer_admission: PASSED\n");
}

// The policy holds max_peer_names peers.  find_peer gives the slot of each
// peer that the policy allows, and no slot to each other name.  The policy
// refuses a peer past the bound.
void test_peer_allowlist_bound() {
    ::foundation::core::FixedArray<cntp::MtlsDnsName, cntp::MtlsPolicy::max_peer_names + 1> const names{
        take(cntp::MtlsDnsName::from("peer-0.example.org")), take(cntp::MtlsDnsName::from("peer-1.example.org")),
        take(cntp::MtlsDnsName::from("peer-2.example.org")), take(cntp::MtlsDnsName::from("peer-3.example.org")),
        take(cntp::MtlsDnsName::from("peer-4.example.org")), take(cntp::MtlsDnsName::from("peer-5.example.org")),
        take(cntp::MtlsDnsName::from("peer-6.example.org")), take(cntp::MtlsDnsName::from("peer-7.example.org")),
        take(cntp::MtlsDnsName::from("peer-8.example.org")),
    };
    cntp::MtlsCertificateFingerprint const pin = make_pin(0x10);

    cntp::MtlsPolicy policy{};
    std::size_t allowed = 0;
    for (cntp::MtlsDnsName const& name : names) {
        CRUCIBLE_TEST_ASSERT(kTest, policy.find_peer(name).is_none());
        auto admitted = policy.allow_peer_with_pin(name, pin);
        if (allowed == cntp::MtlsPolicy::max_peer_names) {
            CRUCIBLE_TEST_ASSERT(kTest, admitted.err() == ::fixy::some(cntp::MtlsError::TooManyPeerNames));
            CRUCIBLE_TEST_ASSERT(kTest, policy.find_peer(name).is_none());
            continue;
        }
        CRUCIBLE_TEST_ASSERT(kTest, admitted.is_ok());
        CRUCIBLE_TEST_ASSERT(kTest, policy.find_peer(name).expect("the policy allows the peer").raw() == allowed);
        ++allowed;
    }
    CRUCIBLE_TEST_ASSERT(kTest, policy.allowed_peer_count.value() == cntp::MtlsPolicy::max_peer_names);
    CRUCIBLE_TEST_ASSERT(kTest, cntp::mtls_policy_allows_peer_name(policy, names.at(::fixy::index<0>())));
    CRUCIBLE_TEST_ASSERT(kTest, !cntp::mtls_policy_allows_peer_name(
                                    policy, names.at(::fixy::index<cntp::MtlsPolicy::max_peer_names>())));

    ::fixy::check::pass(kTest, "  test_peer_allowlist_bound:      PASSED\n");
}

// This pins the exact contract every data-plane entry point offers
// while it is still a stub.  Wiring a real handshake means flipping
// the marker below, replacing the refusal branches with live calls,
// and rewriting this function to exercise the live path.  Doing any
// one of those without the others fails here or at compile time.
void test_data_plane_is_stub() {
    // The marker stays false while the entry points refuse work.  It
    // is a compile-time constant, so a partial flip shows up as a
    // build change a reviewer cannot overlook.
    CRUCIBLE_TEST_ASSERT(kTest, cntp::data_plane_implemented == false);

    // Admission is real even though the data plane is not.  A
    // connection attempt passes validation and only then refuses, so
    // policy truth and wire truth stay separate.
    MaterialBytes const ca_bytes = make_material(0x91);
    MaterialBytes const cert_bytes = make_material(0x92);
    MaterialBytes const key_bytes = make_material(0x93);

    cntp::MtlsCertificate ca = take(cntp::admit_x509_certificate_pem(ca_bytes.view()));
    cntp::MtlsCertificate cert = take(cntp::admit_x509_certificate_pem(cert_bytes.view()));
    cntp::MtlsPrivateKey key = take(cntp::admit_private_key_pem<cntp::MtlsKeyAlgorithm::Ed25519>(key_bytes.view()));

    cntp::MtlsDnsName const peer_dns = take(cntp::MtlsDnsName::from("federation-stub.example.org"));
    cntp::MtlsCertificateFingerprint const pin = make_pin(0x99);

    cntp::MtlsPolicy policy{};
    CRUCIBLE_TEST_ASSERT(kTest, policy.allow_peer_with_pin(peer_dns, pin).is_ok());

    auto config = cntp::mint_mtls_config(std::move(ca), std::move(cert), std::move(key), policy);
    CRUCIBLE_TEST_ASSERT(kTest, cntp::validate_mtls_config(config).is_ok());

    // The stub refuses each connect before it touches the socket, so the
    // empty ref serves.
    cntp::SocketFd const fd{};
    CRUCIBLE_TEST_ASSERT(kTest, cntp::connect_mtls(fd, config, peer_dns, pin).err()
                                    == ::fixy::some(cntp::MtlsError::BackendUnavailable));

    // A peer that fails policy is refused for that reason, not for
    // the missing backend.  Were it the other way round, a genuine
    // policy rejection would hide behind the stub's refusal.
    cntp::MtlsDnsName const unauthorized_dns = take(cntp::MtlsDnsName::from("rogue-peer.example.org"));
    CRUCIBLE_TEST_ASSERT(kTest, cntp::connect_mtls(fd, config, unauthorized_dns, pin).err()
                                    == ::fixy::some(cntp::MtlsError::PeerNameNotAllowed));

    ::fixy::check::pass(kTest, "  test_data_plane_is_stub:        PASSED\n");
}

void test_reject_insecure_policy() {
    cntp::MtlsPolicy policy{};
    policy.verify_peer = false;
    CRUCIBLE_TEST_ASSERT(kTest, cntp::validate_mtls_policy(policy).err()
                                    == ::fixy::some(cntp::MtlsError::PeerVerificationDisabled));

    policy.verify_peer = true;
    policy.require_peer_cert = false;
    CRUCIBLE_TEST_ASSERT(kTest, cntp::validate_mtls_policy(policy).err()
                                    == ::fixy::some(cntp::MtlsError::PeerCertificateNotRequired));

    policy.require_peer_cert = true;
    policy.min_version = cntp::TlsVersion::V12;
    CRUCIBLE_TEST_ASSERT(kTest, cntp::validate_mtls_policy(policy).err()
                                    == ::fixy::some(cntp::MtlsError::UnsupportedTlsVersion));

    MaterialBytes const ca_bytes = make_material(0x31);
    MaterialBytes const cert_bytes = make_material(0x32);
    cntp::MtlsCertificate ca = take(cntp::admit_x509_certificate_pem(ca_bytes.view()));
    cntp::MtlsCertificate cert = take(cntp::admit_x509_certificate_pem(cert_bytes.view()));
    auto empty_key_config =
        cntp::mint_mtls_config(std::move(ca), std::move(cert), ::fixy::mint_secret<cntp::MtlsPrivateKeyBytes>());
    CRUCIBLE_TEST_ASSERT(kTest, cntp::validate_mtls_config(empty_key_config).err()
                                    == ::fixy::some(cntp::MtlsError::EmptyPrivateKey));

    ::fixy::check::pass(kTest, "  test_reject_insecure_policy:   PASSED\n");
}

}  // namespace

int main() {
    static_assert(sizeof(cntp::MtlsCertificate) == sizeof(cntp::MtlsCertificateBytes));
    static_assert(sizeof(cntp::MtlsPrivateKey) == sizeof(cntp::MtlsPrivateKeyBytes));
    static_assert(sizeof(cntp::AuthenticatedMtlsPeer) == sizeof(cntp::MtlsPeerIdentity));
    static_assert(!std::copy_constructible<cntp::MtlsPrivateKeyBytes>);
    static_assert(!std::copy_constructible<cntp::MtlsConfig>);
    static_assert(std::move_constructible<cntp::MtlsConfig>);
    static_assert(cntp::SupportedMtlsVersion<cntp::TlsVersion::V13>);
    static_assert(!cntp::SupportedMtlsVersion<cntp::TlsVersion::V12>);
    static_assert(cntp::ApprovedMtlsCipherSuite<cntp::MtlsCipherSuite::TlsAes256GcmSha384>);
    static_assert(!cntp::ApprovedMtlsCipherSuite<cntp::MtlsCipherSuite::LegacyRsa3desSha>);
    static_assert(cntp::ApprovedMtlsKeyAlgorithm<cntp::MtlsKeyAlgorithm::Ed25519>);
    static_assert(!cntp::ApprovedMtlsKeyAlgorithm<cntp::MtlsKeyAlgorithm::RsaPkcs1>);

    // The same marker, checked at translation time, so that flipping
    // it without rewriting the runtime witness fails the build.
    static_assert(!cntp::data_plane_implemented, "The transport data plane is still a stub.  Setting this "
                                                 "marker true requires a live handshake, live read and write, "
                                                 "live kernel offload, and a rewritten stub witness.");
    static_assert(std::is_same_v<decltype(cntp::data_plane_implemented), const bool>,
                  "The marker must be a compile-time bool.");

    ::fixy::report(::fixy::Sink::Out, "test_cntp_mtls_transport:\n");
    test_name_and_error_surfaces();
    test_material_admission();
    test_policy_and_peer_admission();
    test_peer_allowlist_bound();
    test_data_plane_is_stub();
    test_reject_insecure_policy();
    ::fixy::check::pass(kTest, "test_cntp_mtls_transport: all PASSED\n");
    return 0;
}

#pragma GCC diagnostic pop
