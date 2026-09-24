#include <crucible/cntp/MtlsTransport.h>

// GAPS-126 fixture #3: connect_mtls requires a DeclaredMtlsConfig tagged
// with source::Mtls. Raw configs cannot drive federation transport identity.

int main() {
    using namespace crucible::cntp;
    MtlsConfig raw{
        .ca_cert = MtlsCertificate{MtlsCertificateBytes{}},
        .client_cert = MtlsCertificate{MtlsCertificateBytes{}},
        .client_key = MtlsPrivateKey{MtlsPrivateKeyBytes{}},
        .policy = {},
    };
    auto fd = admit_socket_fd(3).value();
    auto dns = MtlsDnsName::from("peer.example.org").value();
    auto fp = MtlsCertificateFingerprint{MtlsSha256Fingerprint{}};
    auto result = connect_mtls(fd, raw, dns, fp);
    (void)result;
    return 0;
}
