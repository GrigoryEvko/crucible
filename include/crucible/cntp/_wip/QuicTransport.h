#pragma once

// No backend is bound: no msquic, no quiche, no ngtcp2, no kernel QUIC socket,
// and no packet leaves the process.  The runtime operations validate their
// typed inputs and then report QuicError::BackendUnavailable.

#include <crucible/cntp/CongestionControl.h>
#include <crucible/cntp/MtlsTransport.h>
#include <crucible/cntp/PathSwap.h>
#include <fixy/Bits.h>
#include <fixy/Ctx.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <foundation/Pinned.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/reflect/EnumName.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <type_traits>

namespace crucible::cntp::_wip {

using ::crucible::cntp::CcAlgorithm;
using ::crucible::cntp::CcSelection;
using ::crucible::cntp::AuthenticatedMtlsPeer;
using ::crucible::cntp::DeclaredCcChoice;
using ::crucible::cntp::DeclaredMtlsConfig;
using ::crucible::cntp::DeclaredPathSwapPlan;
using ::crucible::cntp::LinkClass;
using ::crucible::cntp::MtlsCertificateFingerprint;
using ::crucible::cntp::MtlsDnsName;
using ::crucible::cntp::MtlsKeyAlgorithm;
using ::crucible::cntp::MtlsPeerIdentity;
using ::crucible::cntp::MtlsPolicy;
using ::crucible::cntp::MtlsSha256Fingerprint;
using ::crucible::cntp::PathSwapPlan;
using ::crucible::cntp::SocketFd;
using ::crucible::cntp::admit_certificate_fingerprint;
using ::crucible::cntp::admit_path_id;
using ::crucible::cntp::admit_private_key_pem;
using ::crucible::cntp::admit_swap_timeout_ns;
using ::crucible::cntp::admit_socket_fd;
using ::crucible::cntp::admit_x509_certificate_pem;
using ::crucible::cntp::mint_cc_choice;
using ::crucible::cntp::mint_mtls_config;
using ::crucible::cntp::mint_path_swap_plan;

namespace wip_source {
struct Quic {};
}  // namespace wip_source

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
    Datagrams = 1u << 0,
    ZeroRtt = 1u << 1,
    Migration = 1u << 2,
    KernelBackend = 1u << 3,
    UserspaceBackend = 1u << 4,
};

enum class QuicStreamKind : std::uint8_t {
    Bidirectional,
    Unidirectional,
};

// The name of an error, a feature or a stream kind is the identifier of its
// enumerator.
[[nodiscard]] constexpr std::string_view quic_error_name(QuicError error) noexcept {
    return ::foundation::reflect::enum_name(error);
}

[[nodiscard]] constexpr std::string_view quic_feature_name(QuicFeature feature) noexcept {
    return ::foundation::reflect::enum_name(feature);
}

[[nodiscard]] constexpr std::string_view quic_stream_kind_name(QuicStreamKind kind) noexcept {
    return ::foundation::reflect::enum_name(kind);
}

// The spelling of each backend that a package and a log line use.
[[nodiscard]] constexpr std::string_view quic_backend_name(QuicBackend backend) noexcept {
    switch (backend) {
        case QuicBackend::KernelMsQuic:
            return "kernel-msquic";
        case QuicBackend::MsQuicUser:
            return "msquic-user";
        case QuicBackend::Quiche:
            return "quiche";
        case QuicBackend::Ngtcp2:
            return "ngtcp2";
        default:
            return "<unknown QuicBackend>";
    }
}

using QuicFeatureMask = ::fixy::Bits<QuicFeature>;
using PositiveQuicStreamLimit = ::fixy::Positive<std::uint16_t>;
using PositiveQuicDatagramBytes = ::fixy::Positive<std::uint32_t>;

struct QuicConfig {
    PositiveQuicStreamLimit max_streams = ::fixy::mint_refined<::fixy::positive>(std::uint16_t{100});
    PositiveQuicDatagramBytes max_datagram_bytes = ::fixy::mint_refined<::fixy::positive>(std::uint32_t{1200});
    DeclaredCcChoice congestion_control = mint_cc_choice<CcAlgorithm::Bbr3, LinkClass::CrossDatacenter>();
    QuicFeatureMask features{
        QuicFeature::Datagrams,
        QuicFeature::Migration,
        QuicFeature::UserspaceBackend,
    };
    QuicBackend preferred_backend = QuicBackend::KernelMsQuic;
};

using DeclaredQuicConfig = ::fixy::Tagged<QuicConfig, wip_source::Quic>;

// The length is private and from() is its only writer, so every token has
// at least one byte and at most max_bytes, and view() never reads past the
// array.  There is no empty token: from() refuses an empty span.
class QuicResumptionToken {
public:
    static constexpr std::size_t max_bytes = 512;

    [[nodiscard]] constexpr std::span<const std::byte> view() const noexcept { return {bytes_.data(), size_}; }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }

    [[nodiscard]] static constexpr std::expected<QuicResumptionToken, QuicError>
    from(std::span<const std::byte> bytes) noexcept {
        if (bytes.empty()) {
            return std::unexpected(QuicError::EmptyResumptionToken);
        }
        if (bytes.size() > max_bytes) {
            return std::unexpected(QuicError::ResumptionTokenTooLarge);
        }
        QuicResumptionToken token{};
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            token.bytes_[i] = bytes[i];
        }
        token.size_ = static_cast<std::uint16_t>(bytes.size());
        return token;
    }

private:
    constexpr QuicResumptionToken() noexcept = default;

    std::array<std::byte, max_bytes> bytes_{};
    std::uint16_t size_ = 0;
};

using DeclaredQuicResumptionToken = ::fixy::Tagged<QuicResumptionToken, wip_source::Quic>;

struct QuicStreamDescriptor {
    std::uint64_t id = 0;
    QuicStreamKind kind = QuicStreamKind::Bidirectional;
};

using DeclaredQuicStream = ::fixy::Tagged<QuicStreamDescriptor, wip_source::Quic>;

struct QuicMigrationPlan {
    PathSwapPlan path_swap;
    std::uint64_t migration_sequence = 0;
};

using DeclaredQuicMigration = ::fixy::Tagged<QuicMigrationPlan, wip_source::Quic>;

// A connection is built at startup.
template <class Ctx>
concept CtxFitsQuicMint = ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Init>;

// A stream, a datagram and a migration are background work.
template <class Ctx>
concept CtxFitsQuicRuntime = ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Bg>;

[[nodiscard]] constexpr std::expected<PositiveQuicStreamLimit, QuicError>
admit_quic_stream_limit(std::uint16_t limit) noexcept {
    return ::fixy::admit_refined<::fixy::positive>(limit, QuicError::InvalidStreamLimit);
}

[[nodiscard]] constexpr std::expected<PositiveQuicDatagramBytes, QuicError>
admit_quic_datagram_bytes(std::uint32_t bytes) noexcept {
    return ::fixy::admit_refined<::fixy::positive>(bytes, QuicError::DatagramEmpty);
}

[[nodiscard]] constexpr std::expected<DeclaredQuicResumptionToken, QuicError>
admit_quic_resumption_token(std::span<const std::byte> bytes) noexcept {
    auto token = QuicResumptionToken::from(bytes);
    if (!token.has_value()) {
        return std::unexpected(token.error());
    }
    return ::fixy::mint_tagged<wip_source::Quic>(*token);
}

[[nodiscard]] constexpr DeclaredQuicConfig
mint_quic_config(PositiveQuicStreamLimit max_streams, PositiveQuicDatagramBytes max_datagram_bytes,
                 DeclaredCcChoice congestion_control,
                 QuicFeatureMask features =
                     {
                         QuicFeature::Datagrams,
                         QuicFeature::Migration,
                         QuicFeature::UserspaceBackend,
                     },
                 QuicBackend preferred_backend = QuicBackend::KernelMsQuic) noexcept {
    return ::fixy::mint_tagged<wip_source::Quic>(QuicConfig{
        .max_streams = max_streams,
        .max_datagram_bytes = max_datagram_bytes,
        .congestion_control = congestion_control,
        .features = features,
        .preferred_backend = preferred_backend,
    });
}

template <std::size_t MaxStreams = 256>
class QuicConnection;

template <std::size_t MaxStreams = 256, class Ctx>
    requires CtxFitsQuicMint<Ctx>
[[nodiscard]] constexpr QuicConnection<MaxStreams> mint_quic_connection(Ctx const& ctx, SocketFd socket,
                                                                        AuthenticatedMtlsPeer peer,
                                                                        DeclaredQuicConfig const& quic_config) noexcept;

template <std::size_t MaxStreams>
class QuicConnection : public ::foundation::Pinned<QuicConnection<MaxStreams>> {
    static_assert(MaxStreams > 0, "QuicConnection requires stream storage");
    static_assert(MaxStreams <= UINT16_MAX, "QuicConnection stream counters are uint16_t bounded");

    SocketFd socket_;
    AuthenticatedMtlsPeer peer_;
    QuicConfig config_;
    std::array<QuicStreamDescriptor, MaxStreams> streams_{};
    std::array<bool, MaxStreams> stream_open_{};
    std::uint16_t open_streams_ = 0;
    std::uint64_t next_bidi_stream_id_ = 0;
    std::uint64_t next_uni_stream_id_ = 2;
    std::uint64_t migration_sequence_ = 0;

    // The mint is the only door, so a connection exists only where a context
    // that owns Init built it.
    constexpr QuicConnection(SocketFd socket, AuthenticatedMtlsPeer peer, DeclaredQuicConfig const& config) noexcept
        : socket_{socket}, peer_{peer}, config_{config.value()} {}

    template <std::size_t N, class Ctx>
        requires CtxFitsQuicMint<Ctx>
    friend constexpr QuicConnection<N> mint_quic_connection(Ctx const& ctx, SocketFd socket, AuthenticatedMtlsPeer peer,
                                                            DeclaredQuicConfig const& quic_config) noexcept;

    [[nodiscard]] constexpr std::uint16_t stream_index(std::uint64_t id) const noexcept {
        for (std::uint16_t i = 0; i < MaxStreams; ++i) {
            if (stream_open_[i] && streams_[i].id == id) {
                return i;
            }
        }
        return UINT16_MAX;
    }

    [[nodiscard]] constexpr std::expected<std::uint16_t, QuicError> vacant_stream_index() const noexcept {
        if (open_streams_ >= config_.max_streams.value() || open_streams_ >= MaxStreams) {
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
    [[nodiscard]] constexpr SocketFd socket() const noexcept { return socket_; }
    [[nodiscard]] constexpr AuthenticatedMtlsPeer const& peer() const noexcept { return peer_; }
    [[nodiscard]] constexpr QuicConfig const& config() const noexcept { return config_; }
    [[nodiscard]] constexpr std::uint16_t open_stream_count() const noexcept { return open_streams_; }
    [[nodiscard]] constexpr std::uint64_t migration_sequence() const noexcept { return migration_sequence_; }

    template <class Ctx>
        requires CtxFitsQuicRuntime<Ctx>
    [[nodiscard]] constexpr std::expected<DeclaredQuicStream, QuicError>
    open_stream(Ctx const&, QuicStreamKind kind = QuicStreamKind::Bidirectional) noexcept {
        auto idx = vacant_stream_index();
        if (!idx.has_value()) {
            return std::unexpected(idx.error());
        }
        const std::uint64_t id = kind == QuicStreamKind::Bidirectional ? next_bidi_stream_id_ : next_uni_stream_id_;
        if (kind == QuicStreamKind::Bidirectional) {
            next_bidi_stream_id_ += 4;
        } else {
            next_uni_stream_id_ += 4;
        }
        streams_[*idx] = QuicStreamDescriptor{.id = id, .kind = kind};
        stream_open_[*idx] = true;
        ++open_streams_;
        return ::fixy::mint_tagged<wip_source::Quic>(streams_[*idx]);
    }

    template <class Ctx>
        requires CtxFitsQuicRuntime<Ctx>
    [[nodiscard]] constexpr std::expected<void, QuicError> close_stream(Ctx const&,
                                                                        DeclaredQuicStream const& stream) noexcept {
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
    [[nodiscard]] constexpr std::expected<void, QuicError> send_datagram(Ctx const&,
                                                                         std::span<const std::byte> payload) noexcept {
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

    // A token always holds at least one byte, so the missing backend is the
    // only refusal left after the feature check.
    template <class Ctx>
        requires CtxFitsQuicRuntime<Ctx>
    [[nodiscard]] constexpr std::expected<void, QuicError> enable_0rtt(Ctx const&,
                                                                       DeclaredQuicResumptionToken const&) noexcept {
        if (!config_.features.test(QuicFeature::ZeroRtt)) {
            return std::unexpected(QuicError::ZeroRttDisabled);
        }
        return std::unexpected(QuicError::BackendUnavailable);
    }

    // A plan always names two different paths, so the plan needs no second
    // check here.
    template <class Ctx>
        requires CtxFitsQuicRuntime<Ctx>
    [[nodiscard]] constexpr std::expected<DeclaredQuicMigration, QuicError>
    plan_migration(Ctx const&, DeclaredPathSwapPlan const& plan) noexcept {
        if (!config_.features.test(QuicFeature::Migration)) {
            return std::unexpected(QuicError::MigrationDisabled);
        }
        ++migration_sequence_;
        return ::fixy::mint_tagged<wip_source::Quic>(QuicMigrationPlan{
            .path_swap = plan.value(),
            .migration_sequence = migration_sequence_,
        });
    }
};

template <std::size_t MaxStreams, class Ctx>
    requires CtxFitsQuicMint<Ctx>
[[nodiscard]] constexpr QuicConnection<MaxStreams>
mint_quic_connection(Ctx const&, SocketFd socket, AuthenticatedMtlsPeer peer,
                     DeclaredQuicConfig const& quic_config) noexcept {
    return QuicConnection<MaxStreams>{socket, peer, quic_config};
}

[[nodiscard]] std::expected<void, QuicError> connect_quic(SocketFd socket, DeclaredMtlsConfig const& mtls_config,
                                                          DeclaredQuicConfig const& quic_config, MtlsDnsName peer_dns,
                                                          MtlsCertificateFingerprint peer_fingerprint) noexcept;

[[nodiscard]] std::expected<AuthenticatedMtlsPeer, QuicError>
admit_quic_peer(DeclaredMtlsConfig const& mtls_config, MtlsDnsName peer_dns,
                MtlsCertificateFingerprint peer_fingerprint) noexcept;

static_assert(sizeof(DeclaredQuicConfig) == sizeof(QuicConfig));
static_assert(sizeof(DeclaredQuicStream) == sizeof(QuicStreamDescriptor));
static_assert(sizeof(DeclaredQuicMigration) == sizeof(QuicMigrationPlan));
// A refined member makes a config or a plan not trivially copyable, because
// no byte route may build a refined value.  A copy still costs what a copy of
// the bytes costs.
static_assert(std::is_trivially_copy_constructible_v<QuicConfig> && std::is_trivially_destructible_v<QuicConfig>);
static_assert(std::is_trivially_copyable_v<QuicStreamDescriptor>);
static_assert(std::is_trivially_copy_constructible_v<QuicMigrationPlan>
              && std::is_trivially_destructible_v<QuicMigrationPlan>);
static_assert(!std::is_default_constructible_v<QuicResumptionToken> && !std::is_aggregate_v<QuicResumptionToken>,
              "QuicResumptionToken::from must be the only writer of a token length");
static_assert(!std::is_default_constructible_v<QuicConnection<>>,
              "mint_quic_connection must be the only door to a connection");
static_assert(CtxFitsQuicMint<::fixy::ColdInitCtx>);
static_assert(!CtxFitsQuicMint<::fixy::BgDrainCtx>);
static_assert(CtxFitsQuicRuntime<::fixy::BgDrainCtx>);
static_assert(!CtxFitsQuicRuntime<::fixy::ColdInitCtx>);
static_assert(!CtxFitsQuicRuntime<::fixy::HotFgCtx>);

}  // namespace crucible::cntp::_wip
