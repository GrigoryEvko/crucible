#include <crucible/cntp/_wip/QuicTransport.h>

#include <array>
#include <cstddef>

// connect_quic takes a config under the Quic tag.  A bare QuicConfig has no
// conversion to the tagged config, so a config that no mint built cannot
// reach a connect.

int main() {
    namespace cntp = crucible::cntp::_wip;

    std::array<std::byte, 64> pem{std::byte{1}};
    auto ca = cntp::admit_x509_certificate_pem(pem).value();
    auto cert = cntp::admit_x509_certificate_pem(pem).value();
    auto key = cntp::admit_private_key_pem<cntp::MtlsKeyAlgorithm::Ed25519>(pem).value();
    auto mtls = cntp::mint_mtls_config(std::move(ca), std::move(cert), std::move(key));
    auto fd = cntp::admit_socket_fd(3).value();
    auto dns = cntp::MtlsDnsName::from("peer.example.org").value();
    cntp::MtlsSha256Fingerprint pin{};
    pin.bytes[0] = std::byte{7};
    auto fingerprint = cntp::admit_certificate_fingerprint(pin).value();
    cntp::QuicConfig raw{};
    auto result = cntp::connect_quic(fd, mtls, raw, dns, fingerprint);
    (void)result;
    return 0;
}
