#pragma once

// GAPS-123.  TCP congestion telemetry substrate.
//
// This header owns the concrete socket-observation and per-link aggregation
// surface for TCP_INFO / TCP_CC_INFO.  It does not choose routes, mutate
// qdisc/sysctl state, or own runtime worker state; those belong to rt policy,
// NicConfig, and the later optimizer owners.  The invariant here is narrow:
// congestion counters cross the runtime boundary only as admitted SocketFd
// values and tagged TcpInfo snapshots, then aggregate into a per-NIC report
// suitable for those concrete runtime owners.

#include <crucible/Platform.h>
#include <crucible/cntp/CongestionControl.h>
#include <crucible/cog/CogIdentity.h>
#include <crucible/observe/HdrHistogram.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/Tagged.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>

namespace crucible::topology {

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

[[nodiscard]] std::string_view congestion_mode_name(CongestionMode mode) noexcept;
[[nodiscard]] std::string_view telemetry_error_name(TelemetryError error) noexcept;

using PositiveBandwidthBps = safety::Positive<std::uint64_t>;
using PositiveMicroseconds = safety::Positive<std::uint64_t>;
using PositiveWindowBytes = safety::Positive<std::uint32_t>;
using PositiveSamplePeriodNs = safety::Positive<std::uint64_t>;

struct BbrFields {
    PositiveBandwidthBps btl_bw_bps{std::uint64_t{1}};
    PositiveMicroseconds rt_prop_us{std::uint64_t{1}};
    std::uint32_t pacing_gain_q8 = 256;
    std::uint32_t cwnd_gain_q8 = 256;
};

struct DctcpFields {
    std::uint32_t alpha_q10 = 0;
    std::uint32_t ecn_mark_ppm = 0;
    bool enabled = false;
};

struct CongestionState {
    cntp::CcAlgorithm algorithm = cntp::CcAlgorithm::Custom;
    PositiveBandwidthBps btl_bw_bps{std::uint64_t{1}};
    PositiveMicroseconds rt_prop_us{std::uint64_t{1}};
    PositiveWindowBytes cwnd_bytes{std::uint32_t{1}};
    PositiveWindowBytes ssthresh_bytes{std::uint32_t{1}};
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

using TcpInfoSnapshot =
    safety::Tagged<CongestionState, safety::source::TcpInfo>;

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
    std::uint32_t bandwidth_drop_ppm = 150'000;
    std::uint32_t min_samples = 100;
};

struct CongestionDrift {
    bool degraded = false;
    std::uint32_t bandwidth_drop_ppm = 0;
    std::uint32_t observed_samples = 0;
};

struct TelemetrySchedule {
    PositiveSamplePeriodNs active_period_ns{std::uint64_t{1'000'000'000}};
    PositiveSamplePeriodNs idle_period_ns{std::uint64_t{10'000'000'000}};
};

[[nodiscard]] constexpr std::expected<PositiveSamplePeriodNs, TelemetryError>
admit_sample_period_ns(std::uint64_t ns) noexcept {
    if (ns == 0) {
        return std::unexpected(TelemetryError::DeadlineOverflow);
    }
    return PositiveSamplePeriodNs{ns, typename PositiveSamplePeriodNs::Trusted{}};
}

[[nodiscard]] constexpr bool is_nic_cog(cog::CogIdentity const& identity) noexcept {
    return identity.kind == cog::CogKind::NicPort ||
           identity.kind == cog::CogKind::NicCard;
}

[[nodiscard]] constexpr std::expected<TcpInfoSnapshot, TelemetryError>
tag_tcp_info_for_test(CongestionState state) noexcept {
    return TcpInfoSnapshot{state};
}

[[nodiscard, gnu::hot]] std::expected<TcpInfoSnapshot, TelemetryError>
harvest_socket(cntp::SocketFd fd) noexcept;

[[nodiscard]] CongestionAggregate
aggregate_congestion(cog::CogIdentity const& nic,
                     std::span<const TcpInfoSnapshot> samples) noexcept;

[[nodiscard]] std::expected<CongestionAggregate, TelemetryError>
harvest_per_link(cog::CogIdentity const& nic,
                 std::span<const cntp::SocketFd> active_fds) noexcept;

[[nodiscard]] constexpr CongestionDrift
detect_congestion_drift(CongestionAggregate const& observed,
                        PositiveBandwidthBps baseline_bps,
                        CongestionDriftPolicy policy = {}) noexcept {
    CongestionDrift drift{
        .observed_samples = observed.sample_count,
    };
    if (observed.sample_count < policy.min_samples ||
        observed.p95_btl_bw_bps >= baseline_bps.value()) {
        return drift;
    }
    const std::uint64_t missing = baseline_bps.value() - observed.p95_btl_bw_bps;
    drift.bandwidth_drop_ppm = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(
            1'000'000,
            (missing * std::uint64_t{1'000'000}) / baseline_bps.value()));
    drift.degraded = drift.bandwidth_drop_ppm >= policy.bandwidth_drop_ppm;
    return drift;
}

static_assert(sizeof(PositiveBandwidthBps) == sizeof(std::uint64_t));
static_assert(sizeof(PositiveMicroseconds) == sizeof(std::uint64_t));
static_assert(sizeof(PositiveWindowBytes) == sizeof(std::uint32_t));
static_assert(sizeof(TcpInfoSnapshot) == sizeof(CongestionState));
static_assert(std::is_trivially_copyable_v<CongestionState>);

}  // namespace crucible::topology
