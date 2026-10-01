#pragma once

#include <crucible/cntp/CongestionControl.h>
#include <crucible/cog/CogIdentity.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/Platform.h>
#include <foundation/effects/Ctx.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <type_traits>

namespace crucible::topology {

// foundation::reflect::enum_name gives the log spelling of both enums.
enum class CongestionMode : std::uint8_t {
    Open = 0,
    Disorder = 1,
    Cwr = 2,
    Recovery = 3,
    Loss = 4,
    BbrStartup = 5,
    BbrDrain = 6,
    BbrProbeBw = 7,
    BbrProbeRtt = 8,
    Unknown = 9,
};

enum class TelemetryError : std::uint8_t {
    InvalidSocketFd,
    InvalidNicCog,
    GetTcpInfoFailed,
    GetCcInfoFailed,
    EmptySampleSet,
    TooManyLinks,
    LinkNotStarted,
    DeadlineOverflow,
};

using PositiveBandwidthBps = ::fixy::Positive<std::uint64_t>;
using PositiveMicroseconds = ::fixy::Positive<std::uint64_t>;
using PositiveWindowBytes = ::fixy::Positive<std::uint32_t>;
using PositiveSamplePeriodNs = ::fixy::Positive<std::uint64_t>;

struct BbrFields {
    PositiveBandwidthBps btl_bw_bps = ::fixy::mint_refined<::fixy::positive>(std::uint64_t{1});
    PositiveMicroseconds rt_prop_us = ::fixy::mint_refined<::fixy::positive>(std::uint64_t{1});
    std::uint32_t pacing_gain_q8 = 256;
    std::uint32_t cwnd_gain_q8 = 256;
};

struct DctcpFields {
    std::uint32_t alpha_q10 = 0;
    std::uint32_t ecn_mark_ppm = 0;
    bool enabled = false;
};

// One per-socket sample of the congestion controller, decoded from the
// kernel TCP information.  The health class of a link is a different
// thing: topology::CongestionState in TopologyGraph.h.
struct CongestionSample {
    cntp::CcAlgorithm algorithm = cntp::CcAlgorithm::Custom;
    PositiveBandwidthBps btl_bw_bps = ::fixy::mint_refined<::fixy::positive>(std::uint64_t{1});
    PositiveMicroseconds rt_prop_us = ::fixy::mint_refined<::fixy::positive>(std::uint64_t{1});
    PositiveWindowBytes cwnd_bytes = ::fixy::mint_refined<::fixy::positive>(std::uint32_t{1});
    PositiveWindowBytes ssthresh_bytes = ::fixy::mint_refined<::fixy::positive>(std::uint32_t{1});
    std::uint32_t retrans_count = 0;
    std::uint32_t lost_count = 0;
    std::uint32_t in_flight_bytes = 0;
    std::uint64_t pacing_rate_bps = 0;
    std::uint64_t max_pacing_rate_bps = 0;
    std::uint32_t delivered = 0;
    std::uint32_t delivered_ce = 0;
    CongestionMode mode = CongestionMode::Unknown;
    BbrFields bbr{};
    DctcpFields dctcp{};
    bool has_bbr = false;
    bool has_dctcp = false;
};

// A sample that the kernel reported through TCP_INFO.  harvest_socket is
// the producer; a test that needs a sample names the source with
// mint_tagged.
using TcpInfoSnapshot = ::fixy::Tagged<CongestionSample, ::fixy::tags::source::TcpInfo>;

struct CongestionAggregate {
    cog::Uuid nic_uuid{};
    std::uint32_t sample_count = 0;
    std::uint64_t p50_rtt_us = 0;
    std::uint64_t p95_rtt_us = 0;
    std::uint64_t p95_btl_bw_bps = 0;
    std::uint64_t mean_btl_bw_bps = 0;
    std::uint64_t total_in_flight_bytes = 0;
    std::uint64_t retrans_count = 0;
    std::uint64_t lost_count = 0;
    std::uint32_t ecn_mark_ppm = 0;
    CongestionMode worst_mode = CongestionMode::Open;
};

struct CongestionDriftPolicy {
    std::uint32_t bandwidth_drop_ppm = 150000;
    std::uint32_t min_samples = 100;
};

struct CongestionDrift {
    bool degraded = false;
    std::uint32_t bandwidth_drop_ppm = 0;
    std::uint32_t observed_samples = 0;
};

struct TelemetrySchedule {
    PositiveSamplePeriodNs active_period_ns = ::fixy::mint_refined<::fixy::positive>(std::uint64_t{1000000000});
    PositiveSamplePeriodNs idle_period_ns = ::fixy::mint_refined<::fixy::positive>(std::uint64_t{10000000000});
};

[[nodiscard]] constexpr std::expected<PositiveSamplePeriodNs, TelemetryError>
admit_sample_period_ns(std::uint64_t ns) noexcept {
    return ::fixy::admit_refined<::fixy::positive>(ns, TelemetryError::DeadlineOverflow);
}

[[nodiscard]] constexpr bool is_nic_cog(cog::CogIdentity const& identity) noexcept {
    return identity.kind == cog::CogKind::NicPort || identity.kind == cog::CogKind::NicCard;
}

[[nodiscard]] CongestionAggregate aggregate_congestion(cog::CogIdentity const& nic,
                                                       std::span<const TcpInfoSnapshot> samples) noexcept;

namespace detail {

// The two harvest bodies.  Each takes the socket option key, so only a
// gated form below reaches getsockopt.
[[nodiscard, gnu::hot]] std::expected<TcpInfoSnapshot, TelemetryError>
harvest_socket_keyed(cntp::detail::SocketOptionKey const& key, cntp::SocketFd fd) noexcept;

[[nodiscard]] std::expected<CongestionAggregate, TelemetryError>
harvest_per_link_keyed(cntp::detail::SocketOptionKey const& key, cog::CogIdentity const& nic,
                       std::span<const cntp::SocketFd> active_fds) noexcept;

}  // namespace detail

// A harvest reads TCP_INFO, TCP_CC_INFO and TCP_CONGESTION through
// getsockopt, so it takes a context that admits the socket option row.
template <::foundation::effects::IsExecCtx Ctx>
    requires cntp::CtxFitsSocketOption<Ctx>
[[nodiscard]] std::expected<TcpInfoSnapshot, TelemetryError> harvest_socket(Ctx const& ctx,
                                                                            cntp::SocketFd fd) noexcept {
    return detail::harvest_socket_keyed(cntp::detail::socket_option_key_(ctx), fd);
}

template <::foundation::effects::IsExecCtx Ctx>
    requires cntp::CtxFitsSocketOption<Ctx>
[[nodiscard]] std::expected<CongestionAggregate, TelemetryError>
harvest_per_link(Ctx const& ctx, cog::CogIdentity const& nic, std::span<const cntp::SocketFd> active_fds) noexcept {
    return detail::harvest_per_link_keyed(cntp::detail::socket_option_key_(ctx), nic, active_fds);
}

[[nodiscard]] constexpr CongestionDrift detect_congestion_drift(CongestionAggregate const& observed,
                                                                PositiveBandwidthBps baseline_bps,
                                                                CongestionDriftPolicy policy = {}) noexcept {
    CongestionDrift drift{
        .observed_samples = observed.sample_count,
    };
    if (observed.sample_count < policy.min_samples || observed.p95_btl_bw_bps >= baseline_bps.value()) {
        return drift;
    }
    constexpr std::uint64_t parts_per_million = 1000000;
    const std::uint64_t missing = baseline_bps.value() - observed.p95_btl_bw_bps;
    const std::uint64_t drop_ppm = (missing * parts_per_million) / baseline_bps.value();
    drift.bandwidth_drop_ppm = static_cast<std::uint32_t>(drop_ppm < parts_per_million ? drop_ppm : parts_per_million);
    drift.degraded = drift.bandwidth_drop_ppm >= policy.bandwidth_drop_ppm;
    return drift;
}

}  // namespace crucible::topology
