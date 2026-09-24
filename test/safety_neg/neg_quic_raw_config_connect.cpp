#include <crucible/cntp/_wip/QuicTransport.h>

// GAPS-128 fixture #1: connect_quic requires a DeclaredQuicConfig tagged
// with _wip::wip_source::Quic. Raw configs cannot drive federation transport setup.

int main() {
    namespace cntp = crucible::cntp::_wip;

    cntp::QuicConfig raw{};
    cntp::MtlsConfig mtls_raw{
        .ca_cert = cntp::MtlsCertificate{cntp::MtlsCertificateBytes{}},
        .client_cert = cntp::MtlsCertificate{cntp::MtlsCertificateBytes{}},
        .client_key = cntp::MtlsPrivateKey{cntp::MtlsPrivateKeyBytes{}},
        .policy = {},
    };
    cntp::DeclaredMtlsConfig mtls{std::move(mtls_raw)};
    auto fd = cntp::admit_socket_fd(3).value();
    auto dns = cntp::MtlsDnsName::from("peer.example.org").value();
    auto fp = cntp::MtlsCertificateFingerprint{cntp::MtlsSha256Fingerprint{}};
    auto result = cntp::connect_quic(fd, mtls, raw, dns, fp);
    (void)result;
    return 0;
}
