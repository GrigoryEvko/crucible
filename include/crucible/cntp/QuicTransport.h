#pragma once

// GAPS-128.  CNT-P QUIC + TLS 1.3 transport substrate.
//
// This header owns typed admission for QUIC policy, stream budgeting,
// datagram bounds, 0-RTT token provenance, and migration plans.  It
// deliberately does not claim an msquic/quiche/ngtcp2 backend, kernel QUIC
// socket, live packet send, or Cipher audit write.  Backend operations return
// explicit deferred/backend errors after validating the typed facts.

#include <crucible/cntp/CongestionControl.h>
#include <crucible/cntp/MtlsTransport.h>
#include <crucible/cntp/PathSwap.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/safety/Bits.h>
#include <crucible/safety/Pinned.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/Tagged.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <type_traits>

namespace crucible::cntp {

enum class QuicError : std::uint8_t {
    InvalidSocketFd,
    InvalidStreamLimit,
    StreamLimitExceeded,
    UnknownStream,
    StreamAlreadyClosed,
    DatagramDisabled,
    DatagramEmpty,
    DatagramTooLarge,
    ZeroRttDisabled,
    EmptyResumptionToken,
    ResumptionTokenTooLarge,
    MigrationDisabled,
    InvalidMigrationPlan,
    MtlsRejected,
    BackendUnavailable,
};

enum class QuicBackend : std::uint8_t {
    KernelMsQuic,
    MsQuicUser,
    Quiche,
    Ngtcp2,
};

enum class QuicFeature : std::uint16_t {
    Datagrams       = 1u << 0,
    ZeroRtt         = 1u << 1,
    Migration       = 1u << 2,
    KernelBackend   = 1u << 3,
    UserspaceBackend= 1u << 4,
};

enum class QuicStreamKind : std::uint8_t {
    Bidirectional,
    Unidirectional,
};

[[nodiscard]] std::string_view quic_error_name(QuicError error) noexcept;
[[nodiscard]] std::string_view quic_backend_name(QuicBackend backend) noexcept;
[[nodiscard]] std::string_view quic_feature_name(QuicFeature feature) noexcept;
[[nodiscard]] std::string_view quic_stream_kind_name(QuicStreamKind kind) noexcept;

using QuicFeatureMask = safety::Bits<QuicFeature>;
using PositiveQuicStreamLimit = safety::Positive<std::uint16_t>;
using PositiveQuicDatagramBytes = safety::Positive<std::uint32_t>;

struct QuicConfig {
    PositiveQuicStreamLimit max_streams{100};
    PositiveQuicDatagramBytes max_datagram_bytes{1'200};
    DeclaredCcChoice congestion_control =
        mint_cc_choice<CcAlgorithm::Bbr3, LinkClass::CrossDatacenter>();
    QuicFeatureMask features{
        QuicFeature::Datagrams,
        QuicFeature::Migration,
        QuicFeature::UserspaceBackend,
    };
    QuicBackend preferred_backend = QuicBackend::KernelMsQuic;
};

using DeclaredQuicConfig =
    safety::Tagged<QuicConfig, safety::source::Quic>;

struct QuicResumptionToken {
    static constexpr std::size_t max_bytes = 512;

    std::array<std::byte, max_bytes> bytes{};
    std::uint16_t size = 0;

    [[nodiscard]] constexpr std::span<const std::byte> view() const noexcept {
        return {bytes.data(), size};
    }
};

using DeclaredQuicResumptionToken =
    safety::Tagged<QuicResumptionToken, safety::source::Quic>;

struct QuicStreamDescriptor {
    std::uint64_t id = 0;
    QuicStreamKind kind = QuicStreamKind::Bidirectional;
};

using DeclaredQuicStream =
    safety::Tagged<QuicStreamDescriptor, safety::source::Quic>;

struct QuicMigrationPlan {
    PathSwapPlan path_swap{};
    std::uint64_t migration_sequence = 0;
};

using DeclaredQuicMigration =
    safety::Tagged<QuicMigrationPlan, safety::source::Quic>;

template <class Ctx>
concept CtxFitsQuicMint =
       effects::IsExecCtx<Ctx>
    && effects::row_contains_v<effects::row_type_of_t<Ctx>,
                               effects::Effect::Init>;

template <class Ctx>
concept CtxFitsQuicRuntime =
       effects::IsExecCtx<Ctx>
    && effects::row_contains_v<effects::row_type_of_t<Ctx>,
                               effects::Effect::Bg>;

[[nodiscard]] constexpr std::expected<PositiveQuicStreamLimit, QuicError>
admit_quic_stream_limit(std::uint16_t limit) noexcept {
    if (limit == 0) {
        return std::unexpected(QuicError::InvalidStreamLimit);
    }
    return PositiveQuicStreamLimit{
        limit, typename PositiveQuicStreamLimit::Trusted{}};
}

[[nodiscard]] constexpr std::expected<PositiveQuicDatagramBytes, QuicError>
admit_quic_datagram_bytes(std::uint32_t bytes) noexcept {
    if (bytes == 0) {
        return std::unexpected(QuicError::DatagramEmpty);
    }
    return PositiveQuicDatagramBytes{
        bytes, typename PositiveQuicDatagramBytes::Trusted{}};
}

[[nodiscard]] constexpr std::expected<DeclaredQuicResumptionToken, QuicError>
admit_quic_resumption_token(std::span<const std::byte> bytes) noexcept {
    if (bytes.empty()) {
        return std::unexpected(QuicError::EmptyResumptionToken);
    }
    if (bytes.size() > QuicResumptionToken::max_bytes) {
        return std::unexpected(QuicError::ResumptionTokenTooLarge);
    }
    QuicResumptionToken token{};
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        token.bytes[i] = bytes[i];
    }
    token.size = static_cast<std::uint16_t>(bytes.size());
    return DeclaredQuicResumptionToken{token};
}

[[nodiscard]] constexpr DeclaredQuicConfig
mint_quic_config(PositiveQuicStreamLimit max_streams,
                 PositiveQuicDatagramBytes max_datagram_bytes,
                 DeclaredCcChoice congestion_control,
                 QuicFeatureMask features = {
                     QuicFeature::Datagrams,
                     QuicFeature::Migration,
                     QuicFeature::UserspaceBackend,
                 },
                 QuicBackend preferred_backend = QuicBackend::KernelMsQuic) noexcept {
    return DeclaredQuicConfig{QuicConfig{
        .max_streams = max_streams,
        .max_datagram_bytes = max_datagram_bytes,
        .congestion_control = congestion_control,
        .features = features,
        .preferred_backend = preferred_backend,
    }};
}

[[nodiscard]] constexpr std::expected<void, QuicError>
validate_quic_config(DeclaredQuicConfig config) noexcept {
    auto const& raw = config.value();
    if (raw.max_streams.value() == 0) {
        return std::unexpected(QuicError::InvalidStreamLimit);
    }
    if (raw.max_datagram_bytes.value() == 0) {
        return std::unexpected(QuicError::DatagramEmpty);
    }
    return {};
}

template <std::size_t MaxStreams = 256>
class QuicConnection : public safety::Pinned<QuicConnection<MaxStreams>> {
    static_assert(MaxStreams > 0, "QuicConnection requires stream storage");
    static_assert(MaxStreams <= UINT16_MAX,
                  "QuicConnection stream counters are uint16_t bounded");

    SocketFd socket_;
    AuthenticatedMtlsPeer peer_;
    QuicConfig config_;
    std::array<QuicStreamDescriptor, MaxStreams> streams_{};
    std::array<bool, MaxStreams> stream_open_{};
    std::uint16_t open_streams_ = 0;
    std::uint64_t next_bidi_stream_id_ = 0;
    std::uint64_t next_uni_stream_id_ = 2;
    std::uint64_t migration_sequence_ = 0;

    [[nodiscard]] constexpr std::uint16_t stream_index(std::uint64_t id) const noexcept {
        for (std::uint16_t i = 0; i < MaxStreams; ++i) {
            if (stream_open_[i] && streams_[i].id == id) {
                return i;
            }
        }
        return UINT16_MAX;
    }

    [[nodiscard]] constexpr std::expected<std::uint16_t, QuicError>
    vacant_stream_index() const noexcept {
        if (open_streams_ >= config_.max_streams.value() ||
            open_streams_ >= MaxStreams) {
            return std::unexpected(QuicError::StreamLimitExceeded);
        }
        for (std::uint16_t i = 0; i < MaxStreams; ++i) {
            if (!stream_open_[i]) {
                return i;
            }
        }
        return std::unexpected(QuicError::StreamLimitExceeded);
    }

public:
    constexpr QuicConnection(SocketFd socket,
                             AuthenticatedMtlsPeer peer,
                             DeclaredQuicConfig config) noexcept
        : socket_{socket},
          peer_{peer},
          config_{config.value()} {}

    [[nodiscard]] constexpr SocketFd socket() const noexcept { return socket_; }
    [[nodiscard]] constexpr AuthenticatedMtlsPeer const& peer() const noexcept {
        return peer_;
    }
    [[nodiscard]] constexpr QuicConfig const& config() const noexcept {
        return config_;
    }
    [[nodiscard]] constexpr std::uint16_t open_stream_count() const noexcept {
        return open_streams_;
    }
    [[nodiscard]] constexpr std::uint64_t migration_sequence() const noexcept {
        return migration_sequence_;
    }

    template <class Ctx>
        requires CtxFitsQuicRuntime<Ctx>
    [[nodiscard]] constexpr std::expected<DeclaredQuicStream, QuicError>
    open_stream(Ctx const&, QuicStreamKind kind = QuicStreamKind::Bidirectional) noexcept {
        auto idx = vacant_stream_index();
        if (!idx.has_value()) {
            return std::unexpected(idx.error());
        }
        const std::uint64_t id = kind == QuicStreamKind::Bidirectional
            ? next_bidi_stream_id_
            : next_uni_stream_id_;
        if (kind == QuicStreamKind::Bidirectional) {
            next_bidi_stream_id_ += 4;
        } else {
            next_uni_stream_id_ += 4;
        }
        streams_[*idx] = QuicStreamDescriptor{.id = id, .kind = kind};
        stream_open_[*idx] = true;
        ++open_streams_;
        return DeclaredQuicStream{streams_[*idx]};
    }

    template <class Ctx>
        requires CtxFitsQuicRuntime<Ctx>
    [[nodiscard]] constexpr std::expected<void, QuicError>
    close_stream(Ctx const&, DeclaredQuicStream stream) noexcept {
        const std::uint16_t idx = stream_index(stream.value().id);
        if (idx == UINT16_MAX) {
            return std::unexpected(QuicError::UnknownStream);
        }
        stream_open_[idx] = false;
        --open_streams_;
        return {};
    }

    template <class Ctx>
        requires CtxFitsQuicRuntime<Ctx>
    [[nodiscard]] constexpr std::expected<void, QuicError>
    send_datagram(Ctx const&, std::span<const std::byte> payload) noexcept {
        if (!config_.features.test(QuicFeature::Datagrams)) {
            return std::unexpected(QuicError::DatagramDisabled);
        }
        if (payload.empty()) {
            return std::unexpected(QuicError::DatagramEmpty);
        }
        if (payload.size() > config_.max_datagram_bytes.value()) {
            return std::unexpected(QuicError::DatagramTooLarge);
        }
        return std::unexpected(QuicError::BackendUnavailable);
    }

    template <class Ctx>
        requires CtxFitsQuicRuntime<Ctx>
    [[nodiscard]] constexpr std::expected<void, QuicError>
    enable_0rtt(Ctx const&, DeclaredQuicResumptionToken token) noexcept {
        if (!config_.features.test(QuicFeature::ZeroRtt)) {
            return std::unexpected(QuicError::ZeroRttDisabled);
        }
        if (token.value().size == 0) {
            return std::unexpected(QuicError::EmptyResumptionToken);
        }
        return std::unexpected(QuicError::BackendUnavailable);
    }

    template <class Ctx>
        requires CtxFitsQuicRuntime<Ctx>
    [[nodiscard]] constexpr std::expected<DeclaredQuicMigration, QuicError>
    plan_migration(Ctx const&, DeclaredPathSwapPlan plan) noexcept {
        if (!config_.features.test(QuicFeature::Migration)) {
            return std::unexpected(QuicError::MigrationDisabled);
        }
        if (plan.value().old_path.value() == plan.value().new_path.value()) {
            return std::unexpected(QuicError::InvalidMigrationPlan);
        }
        ++migration_sequence_;
        return DeclaredQuicMigration{QuicMigrationPlan{
            .path_swap = plan.value(),
            .migration_sequence = migration_sequence_,
        }};
    }
};

template <std::size_t MaxStreams = 256, class Ctx>
    requires CtxFitsQuicMint<Ctx>
[[nodiscard]] constexpr QuicConnection<MaxStreams>
mint_quic_connection(Ctx const&,
                     SocketFd socket,
                     AuthenticatedMtlsPeer peer,
                     DeclaredQuicConfig quic_config) noexcept {
    static_cast<void>(validate_quic_config(quic_config));
    return QuicConnection<MaxStreams>{socket, peer, quic_config};
}

[[nodiscard]] std::expected<void, QuicError>
connect_quic(SocketFd socket,
             DeclaredMtlsConfig const& mtls_config,
             DeclaredQuicConfig quic_config,
             MtlsDnsName peer_dns,
             MtlsCertificateFingerprint peer_fingerprint) noexcept;

[[nodiscard]] std::expected<AuthenticatedMtlsPeer, QuicError>
admit_quic_peer(DeclaredMtlsConfig const& mtls_config,
                MtlsDnsName peer_dns,
                MtlsCertificateFingerprint peer_fingerprint) noexcept;

static_assert(sizeof(DeclaredQuicConfig) == sizeof(QuicConfig));
static_assert(sizeof(DeclaredQuicStream) == sizeof(QuicStreamDescriptor));
static_assert(sizeof(DeclaredQuicMigration) == sizeof(QuicMigrationPlan));
static_assert(std::is_trivially_copyable_v<QuicConfig>);
static_assert(std::is_trivially_copyable_v<QuicStreamDescriptor>);
static_assert(std::is_trivially_copyable_v<QuicMigrationPlan>);
static_assert(CtxFitsQuicMint<effects::ColdInitCtx>);
static_assert(CtxFitsQuicRuntime<effects::BgDrainCtx>);

}  // namespace crucible::cntp
