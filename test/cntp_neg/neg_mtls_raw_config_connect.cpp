#include <crucible/cntp/MtlsTransport.h>

// connect_mtls requires a DeclaredMtlsConfig tagged with source::Mtls.  A
// raw config cannot drive the transport identity of a federation peer.
// Every other argument comes from its door, so the tag is the only refusal.

int main() {
    using namespace crucible::cntp;
    MtlsConfig raw{
        .ca_cert = ::fixy::mint_linear<MtlsCertificateBytes>(),
        .client_cert = ::fixy::mint_linear<MtlsCertificateBytes>(),
        .client_key = ::fixy::mint_secret<MtlsPrivateKeyBytes>(),
        .policy = {},
    };
    auto fd = admit_socket_fd(3).value();
    auto dns = MtlsDnsName::from("peer.example.org").value();
    auto fp = ::fixy::mint_tagged<::fixy::tags::source::Mtls>(MtlsSha256Fingerprint{});
    auto result = connect_mtls(fd, raw, dns, fp);
    (void)result;
    return 0;
}
