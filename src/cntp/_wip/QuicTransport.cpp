#include <crucible/cntp/_wip/QuicTransport.h>

namespace crucible::cntp::_wip {

std::expected<AuthenticatedMtlsPeer, QuicError> admit_quic_peer(DeclaredMtlsConfig const& mtls_config,
                                                                MtlsDnsName peer_dns,
                                                                MtlsCertificateFingerprint peer_fingerprint) noexcept {
    auto peer =
        admit_mtls_peer_from_handshake(mtls_config, peer_dns, peer_fingerprint, MtlsCipherSuite::TlsAes256GcmSha384);
    if (!peer.has_value()) {
        return std::unexpected(QuicError::MtlsRejected);
    }
    return *peer;
}

// The typed inputs are the whole check: the config fields are refined, so
// only the peer is left to admit before the missing backend refuses.
std::expected<void, QuicError> connect_quic(SocketFd socket, DeclaredMtlsConfig const& mtls_config,
                                            DeclaredQuicConfig const& quic_config, MtlsDnsName peer_dns,
                                            MtlsCertificateFingerprint peer_fingerprint) noexcept {
    static_cast<void>(socket);
    static_cast<void>(quic_config);
    auto peer = admit_quic_peer(mtls_config, peer_dns, peer_fingerprint);
    if (!peer.has_value()) {
        return std::unexpected(peer.error());
    }
    return std::unexpected(QuicError::BackendUnavailable);
}

}  // namespace crucible::cntp::_wip
