#include <crucible/cntp/QuicTransport.h>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <span>
#include <string_view>
#include <type_traits>

namespace cntp = crucible::cntp;
namespace effects = crucible::effects;
namespace saf = crucible::safety;

namespace {

[[nodiscard]] std::array<std::byte, 96> material(std::byte seed) {
    std::array<std::byte, 96> out{};
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = static_cast<std::byte>(
            std::to_integer<unsigned>(seed) + static_cast<unsigned>(i + 1u));
    }
    return out;
}

[[nodiscard]] std::array<std::byte, 32> fingerprint_bytes(std::byte seed) {
    std::array<std::byte, 32> out{};
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = static_cast<std::byte>(
            std::to_integer<unsigned>(seed) ^ static_cast<unsigned>(i * 7u));
    }
    return out;
}

[[nodiscard]] cntp::MtlsCertificateFingerprint fingerprint(std::byte seed) {
    cntp::MtlsSha256Fingerprint raw{fingerprint_bytes(seed)};
    auto admitted = cntp::admit_certificate_fingerprint(raw);
    assert(admitted.has_value());
    return *admitted;
}

[[nodiscard]] cntp::DeclaredMtlsConfig mtls_config(cntp::MtlsDnsName peer) {
    auto ca_bytes = material(std::byte{0x10});
    auto cert_bytes = material(std::byte{0x20});
    auto key_bytes = material(std::byte{0x30});
    auto ca = cntp::admit_x509_certificate_pem(ca_bytes);
    auto cert = cntp::admit_x509_certificate_pem(cert_bytes);
    auto key = cntp::admit_private_key_pem<cntp::MtlsKeyAlgorithm::Ed25519>(
        key_bytes);
    assert(ca.has_value());
    assert(cert.has_value());
    assert(key.has_value());

    cntp::MtlsPolicy policy{};
    auto fp = fingerprint(std::byte{0x41});
    assert(policy.allow_peer_with_pin(peer, fp).has_value());
    return cntp::mint_mtls_config(
        std::move(*ca), std::move(*cert), std::move(*key), policy);
}

[[nodiscard]] cntp::DeclaredQuicConfig quic_config(
    std::uint16_t streams = 2,
    cntp::QuicFeatureMask features = {
        cntp::QuicFeature::Datagrams,
        cntp::QuicFeature::Migration,
        cntp::QuicFeature::ZeroRtt,
        cntp::QuicFeature::UserspaceBackend,
    }) {
    auto max_streams = cntp::admit_quic_stream_limit(streams);
    auto max_datagram = cntp::admit_quic_datagram_bytes(32);
    assert(max_streams.has_value());
    assert(max_datagram.has_value());
    return cntp::mint_quic_config(
        *max_streams,
        *max_datagram,
        cntp::mint_cc_choice<cntp::CcAlgorithm::Bbr3,
                             cntp::LinkClass::CrossDatacenter>(),
        features,
        cntp::QuicBackend::Quiche);
}

[[nodiscard]] cntp::DeclaredPathSwapPlan path_plan() {
    auto flow = cntp::admit_path_id(10).value();
    auto old_path = cntp::admit_path_id(20).value();
    auto new_path = cntp::admit_path_id(30).value();
    auto timeout = cntp::admit_swap_timeout_ns(1'000).value();
    auto plan = cntp::mint_path_swap_plan(flow, old_path, new_path, timeout);
    assert(plan.has_value());
    return *plan;
}

void test_name_surfaces() {
    assert(cntp::quic_error_name(cntp::QuicError::BackendUnavailable) ==
           std::string_view{"BackendUnavailable"});
    assert(cntp::quic_backend_name(cntp::QuicBackend::Ngtcp2) ==
           std::string_view{"ngtcp2"});
    assert(cntp::quic_feature_name(cntp::QuicFeature::Migration) ==
           std::string_view{"Migration"});
    assert(cntp::quic_stream_kind_name(cntp::QuicStreamKind::Unidirectional) ==
           std::string_view{"Unidirectional"});
    std::printf("  test_name_surfaces: PASSED\n");
}

void test_admission_and_backend_boundary() {
    auto zero_streams = cntp::admit_quic_stream_limit(0);
    assert(!zero_streams.has_value());
    assert(zero_streams.error() == cntp::QuicError::InvalidStreamLimit);

    std::array<std::byte, 0> empty{};
    auto empty_token = cntp::admit_quic_resumption_token(empty);
    assert(!empty_token.has_value());
    assert(empty_token.error() == cntp::QuicError::EmptyResumptionToken);

    auto peer = cntp::MtlsDnsName::from("peer-a.example.org");
    assert(peer.has_value());
    auto mtls = mtls_config(*peer);
    auto fd = cntp::admit_socket_fd(3);
    assert(fd.has_value());
    auto config = quic_config();
    auto connect = cntp::connect_quic(*fd, mtls, config, *peer,
                                      fingerprint(std::byte{0x41}));
    assert(!connect.has_value());
    assert(connect.error() == cntp::QuicError::BackendUnavailable);

    auto denied = cntp::MtlsDnsName::from("peer-b.example.org");
    assert(denied.has_value());
    auto bad_peer = cntp::admit_quic_peer(
        mtls, *denied, fingerprint(std::byte{0x41}));
    assert(!bad_peer.has_value());
    assert(bad_peer.error() == cntp::QuicError::MtlsRejected);

    std::printf("  test_admission_and_backend_boundary: PASSED\n");
}

void test_stream_budgeting() {
    effects::ColdInitCtx init{};
    effects::BgDrainCtx bg{};
    auto peer_name = cntp::MtlsDnsName::from("peer-stream.example.org");
    assert(peer_name.has_value());
    auto mtls = mtls_config(*peer_name);
    auto peer = cntp::admit_quic_peer(
        mtls, *peer_name, fingerprint(std::byte{0x41}));
    assert(peer.has_value());
    auto fd = cntp::admit_socket_fd(4).value();

    auto connection = cntp::mint_quic_connection<2>(
        init, fd, *peer, quic_config(2));
    auto first = connection.open_stream(bg);
    auto second = connection.open_stream(bg, cntp::QuicStreamKind::Unidirectional);
    assert(first.has_value());
    assert(second.has_value());
    assert(first->value().id == 0);
    assert(second->value().id == 2);
    assert(connection.open_stream_count() == 2);

    auto overflow = connection.open_stream(bg);
    assert(!overflow.has_value());
    assert(overflow.error() == cntp::QuicError::StreamLimitExceeded);

    auto close = connection.close_stream(bg, *first);
    assert(close.has_value());
    assert(connection.open_stream_count() == 1);
    auto replacement = connection.open_stream(bg);
    assert(replacement.has_value());
    assert(replacement->value().id == 4);

    std::printf("  test_stream_budgeting: PASSED\n");
}

void test_datagram_zero_rtt_and_migration_plans() {
    effects::ColdInitCtx init{};
    effects::BgDrainCtx bg{};
    auto peer_name = cntp::MtlsDnsName::from("peer-data.example.org");
    assert(peer_name.has_value());
    auto mtls = mtls_config(*peer_name);
    auto peer = cntp::admit_quic_peer(
        mtls, *peer_name, fingerprint(std::byte{0x41}));
    assert(peer.has_value());
    auto fd = cntp::admit_socket_fd(5).value();

    auto connection = cntp::mint_quic_connection<4>(
        init, fd, *peer, quic_config());
    std::array<std::byte, 8> payload{};
    auto send = connection.send_datagram(bg, payload);
    assert(!send.has_value());
    assert(send.error() == cntp::QuicError::BackendUnavailable);

    std::array<std::byte, 64> too_large{};
    auto large = connection.send_datagram(bg, too_large);
    assert(!large.has_value());
    assert(large.error() == cntp::QuicError::DatagramTooLarge);

    auto token = cntp::admit_quic_resumption_token(payload);
    assert(token.has_value());
    auto zero_rtt = connection.enable_0rtt(bg, *token);
    assert(!zero_rtt.has_value());
    assert(zero_rtt.error() == cntp::QuicError::BackendUnavailable);

    auto migration = connection.plan_migration(bg, path_plan());
    assert(migration.has_value());
    assert(migration->value().migration_sequence == 1);
    assert(connection.migration_sequence() == 1);

    auto disabled_config = quic_config(2, {cntp::QuicFeature::UserspaceBackend});
    auto disabled = cntp::mint_quic_connection<2>(
        init, fd, *peer, disabled_config);
    assert(disabled.send_datagram(bg, payload).error() ==
           cntp::QuicError::DatagramDisabled);
    assert(disabled.enable_0rtt(bg, *token).error() ==
           cntp::QuicError::ZeroRttDisabled);
    assert(disabled.plan_migration(bg, path_plan()).error() ==
           cntp::QuicError::MigrationDisabled);

    std::printf("  test_datagram_zero_rtt_and_migration_plans: PASSED\n");
}

}  // namespace

int main() {
    static_assert(sizeof(cntp::DeclaredQuicConfig) == sizeof(cntp::QuicConfig));
    static_assert(sizeof(cntp::DeclaredQuicStream) ==
                  sizeof(cntp::QuicStreamDescriptor));
    static_assert(std::same_as<
                  cntp::DeclaredQuicConfig::tag_type,
                  saf::source::Quic>);
    static_assert(cntp::CtxFitsQuicMint<effects::ColdInitCtx>);
    static_assert(!cntp::CtxFitsQuicMint<effects::BgDrainCtx>);
    static_assert(cntp::CtxFitsQuicRuntime<effects::BgDrainCtx>);
    static_assert(!cntp::CtxFitsQuicRuntime<effects::HotFgCtx>);

    std::printf("test_cntp_quic_transport:\n");
    test_name_surfaces();
    test_admission_and_backend_boundary();
    test_stream_budgeting();
    test_datagram_zero_rtt_and_migration_plans();
    std::printf("test_cntp_quic_transport: all PASSED\n");
    return 0;
}
