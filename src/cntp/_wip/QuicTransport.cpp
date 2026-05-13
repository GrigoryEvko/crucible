#include <crucible/cntp/_wip/QuicTransport.h>

namespace crucible::cntp::_wip {

std::string_view quic_error_name(QuicError error) noexcept {
    switch (error) {
        case QuicError::InvalidSocketFd:       return "InvalidSocketFd";
        case QuicError::InvalidStreamLimit:    return "InvalidStreamLimit";
        case QuicError::StreamLimitExceeded:   return "StreamLimitExceeded";
        case QuicError::UnknownStream:         return "UnknownStream";
        case QuicError::StreamAlreadyClosed:   return "StreamAlreadyClosed";
        case QuicError::DatagramDisabled:      return "DatagramDisabled";
        case QuicError::DatagramEmpty:         return "DatagramEmpty";
        case QuicError::DatagramTooLarge:      return "DatagramTooLarge";
        case QuicError::ZeroRttDisabled:       return "ZeroRttDisabled";
        case QuicError::EmptyResumptionToken:  return "EmptyResumptionToken";
        case QuicError::ResumptionTokenTooLarge:return "ResumptionTokenTooLarge";
        case QuicError::MigrationDisabled:     return "MigrationDisabled";
        case QuicError::InvalidMigrationPlan:  return "InvalidMigrationPlan";
        case QuicError::MtlsRejected:          return "MtlsRejected";
        case QuicError::BackendUnavailable:    return "BackendUnavailable";
        default:                               return "<unknown QuicError>";
    }
}

std::string_view quic_backend_name(QuicBackend backend) noexcept {
    switch (backend) {
        case QuicBackend::KernelMsQuic: return "kernel-msquic";
        case QuicBackend::MsQuicUser:   return "msquic-user";
        case QuicBackend::Quiche:       return "quiche";
        case QuicBackend::Ngtcp2:       return "ngtcp2";
        default:                        return "<unknown QuicBackend>";
    }
}

std::string_view quic_feature_name(QuicFeature feature) noexcept {
    switch (feature) {
        case QuicFeature::Datagrams:       return "Datagrams";
        case QuicFeature::ZeroRtt:         return "ZeroRtt";
        case QuicFeature::Migration:       return "Migration";
        case QuicFeature::KernelBackend:   return "KernelBackend";
        case QuicFeature::UserspaceBackend:return "UserspaceBackend";
        default:                           return "<unknown QuicFeature>";
    }
}

std::string_view quic_stream_kind_name(QuicStreamKind kind) noexcept {
    switch (kind) {
        case QuicStreamKind::Bidirectional: return "Bidirectional";
        case QuicStreamKind::Unidirectional: return "Unidirectional";
        default:                            return "<unknown QuicStreamKind>";
    }
}

std::expected<AuthenticatedMtlsPeer, QuicError>
admit_quic_peer(DeclaredMtlsConfig const& mtls_config,
                MtlsDnsName peer_dns,
                MtlsCertificateFingerprint peer_fingerprint) noexcept {
    auto peer = admit_mtls_peer_from_handshake(
        mtls_config,
        peer_dns,
        peer_fingerprint,
        MtlsCipherSuite::TlsAes256GcmSha384);
    if (!peer.has_value()) {
        return std::unexpected(QuicError::MtlsRejected);
    }
    return *peer;
}

std::expected<void, QuicError>
connect_quic(SocketFd socket,
             DeclaredMtlsConfig const& mtls_config,
             DeclaredQuicConfig quic_config,
             MtlsDnsName peer_dns,
             MtlsCertificateFingerprint peer_fingerprint) noexcept {
    static_cast<void>(socket);
    auto valid = validate_quic_config(quic_config);
    if (!valid.has_value()) {
        return std::unexpected(valid.error());
    }
    auto peer = admit_quic_peer(mtls_config, peer_dns, peer_fingerprint);
    if (!peer.has_value()) {
        return std::unexpected(peer.error());
    }
    return std::unexpected(QuicError::BackendUnavailable);
}

}  // namespace crucible::cntp::_wip
