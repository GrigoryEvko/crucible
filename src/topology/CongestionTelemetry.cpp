#include <crucible/topology/CongestionTelemetry.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

#include <linux/inet_diag.h>
#include <linux/tcp.h>
#include <netinet/in.h>
#include <sys/socket.h>

namespace crucible::topology {

namespace {

using RttHistogram = observe::HdrHistogram<2, 3'600'000'000'000ull>;
using BandwidthHistogram = observe::HdrHistogram<2, 1'000'000'000'000ull>;

[[nodiscard]] constexpr std::uint64_t positive_or_one_u64(std::uint64_t v) noexcept {
    return v == 0 ? std::uint64_t{1} : v;
}

[[nodiscard]] constexpr std::uint32_t positive_or_one_u32(std::uint32_t v) noexcept {
    return v == 0 ? std::uint32_t{1} : v;
}

[[nodiscard]] constexpr std::uint32_t sat_u32_mul(std::uint32_t a,
                                                  std::uint32_t b) noexcept {
    const std::uint64_t product = static_cast<std::uint64_t>(a) * b;
    return product > std::numeric_limits<std::uint32_t>::max()
        ? std::numeric_limits<std::uint32_t>::max()
        : static_cast<std::uint32_t>(product);
}

[[nodiscard]] constexpr CongestionMode mode_from_ca_state(std::uint8_t state) noexcept {
    switch (state) {
        case TCP_CA_Open:     return CongestionMode::Open;
        case TCP_CA_Disorder: return CongestionMode::Disorder;
        case TCP_CA_CWR:      return CongestionMode::Cwr;
        case TCP_CA_Recovery: return CongestionMode::Recovery;
        case TCP_CA_Loss:     return CongestionMode::Loss;
        default:              return CongestionMode::Unknown;
    }
}

[[nodiscard]] constexpr CongestionMode mode_from_bbr(BbrFields bbr) noexcept {
    if (bbr.rt_prop_us.value() <= 1) {
        return CongestionMode::BbrProbeRtt;
    }
    if (bbr.pacing_gain_q8 > 300 && bbr.cwnd_gain_q8 >= 512) {
        return CongestionMode::BbrStartup;
    }
    if (bbr.pacing_gain_q8 < 256 && bbr.cwnd_gain_q8 >= 512) {
        return CongestionMode::BbrDrain;
    }
    return CongestionMode::BbrProbeBw;
}

[[nodiscard]] constexpr std::uint8_t mode_severity(CongestionMode mode) noexcept {
    switch (mode) {
        case CongestionMode::Open:        return 0;
        case CongestionMode::BbrProbeBw:  return 1;
        case CongestionMode::BbrStartup:  return 1;
        case CongestionMode::BbrProbeRtt: return 2;
        case CongestionMode::BbrDrain:    return 2;
        case CongestionMode::Disorder:    return 3;
        case CongestionMode::Cwr:         return 4;
        case CongestionMode::Recovery:    return 5;
        case CongestionMode::Loss:        return 6;
        case CongestionMode::Unknown:     return 1;
        default:                          return 1;
    }
}

[[nodiscard]] constexpr bool mode_worse(CongestionMode lhs,
                                        CongestionMode rhs) noexcept {
    return mode_severity(lhs) > mode_severity(rhs);
}

[[nodiscard]] constexpr BbrFields decode_bbr(tcp_bbr_info const& info) noexcept {
    const std::uint64_t bw =
        (static_cast<std::uint64_t>(info.bbr_bw_hi) << 32u) | info.bbr_bw_lo;
    return BbrFields{
        .btl_bw_bps = PositiveBandwidthBps{
            positive_or_one_u64(bw), typename PositiveBandwidthBps::Trusted{}},
        .rt_prop_us = PositiveMicroseconds{
            positive_or_one_u32(info.bbr_min_rtt), typename PositiveMicroseconds::Trusted{}},
        .pacing_gain_q8 = info.bbr_pacing_gain,
        .cwnd_gain_q8 = info.bbr_cwnd_gain,
    };
}

[[nodiscard]] constexpr DctcpFields decode_dctcp(tcp_dctcp_info const& info) noexcept {
    std::uint32_t ppm = 0;
    if (info.dctcp_ab_tot != 0) {
        const std::uint64_t scaled =
            (static_cast<std::uint64_t>(info.dctcp_ab_ecn) * 1'000'000ull)
            / info.dctcp_ab_tot;
        ppm = static_cast<std::uint32_t>(std::min<std::uint64_t>(scaled, 1'000'000));
    }
    return DctcpFields{
        .alpha_q10 = info.dctcp_alpha,
        .ecn_mark_ppm = ppm,
        .enabled = info.dctcp_enabled != 0,
    };
}

[[nodiscard]] CongestionState decode_tcp_info(tcp_info const& info,
                                              cntp::CcAlgorithm algorithm) noexcept {
    const std::uint32_t mss = positive_or_one_u32(info.tcpi_snd_mss);
    const std::uint32_t cwnd_bytes = sat_u32_mul(positive_or_one_u32(info.tcpi_snd_cwnd), mss);
    const std::uint32_t ssthresh_packets =
        info.tcpi_snd_ssthresh == std::numeric_limits<std::uint32_t>::max()
            ? info.tcpi_snd_cwnd
            : positive_or_one_u32(info.tcpi_snd_ssthresh);
    const std::uint32_t ssthresh_bytes = sat_u32_mul(ssthresh_packets, mss);
    const std::uint32_t in_flight = sat_u32_mul(info.tcpi_unacked, mss);
    const std::uint64_t delivery_rate = positive_or_one_u64(info.tcpi_delivery_rate);
    const std::uint64_t rtt = positive_or_one_u64(
        info.tcpi_min_rtt != 0 ? info.tcpi_min_rtt : info.tcpi_rtt);

    CongestionState state{
        .algorithm = algorithm,
        .btl_bw_bps = PositiveBandwidthBps{
            delivery_rate, typename PositiveBandwidthBps::Trusted{}},
        .rt_prop_us = PositiveMicroseconds{
            rtt, typename PositiveMicroseconds::Trusted{}},
        .cwnd_bytes = PositiveWindowBytes{
            positive_or_one_u32(cwnd_bytes), typename PositiveWindowBytes::Trusted{}},
        .ssthresh_bytes = PositiveWindowBytes{
            positive_or_one_u32(ssthresh_bytes), typename PositiveWindowBytes::Trusted{}},
        .retrans_count = info.tcpi_total_retrans,
        .lost_count = info.tcpi_lost,
        .in_flight_bytes = in_flight,
        .pacing_rate_bps = info.tcpi_pacing_rate,
        .max_pacing_rate_bps = info.tcpi_max_pacing_rate,
        .delivered = info.tcpi_delivered,
        .delivered_ce = info.tcpi_delivered_ce,
        .mode = mode_from_ca_state(info.tcpi_ca_state),
    };

    if (state.delivered != 0) {
        const std::uint64_t scaled =
            (static_cast<std::uint64_t>(state.delivered_ce) * 1'000'000ull)
            / state.delivered;
        state.dctcp.ecn_mark_ppm =
            static_cast<std::uint32_t>(std::min<std::uint64_t>(scaled, 1'000'000));
    }
    return state;
}

void record_sample(CongestionAggregate& aggregate,
                   RttHistogram& rtt_hist,
                   BandwidthHistogram& bw_hist,
                   TcpInfoSnapshot const& snapshot) noexcept {
    auto const& state = snapshot.value();
    ++aggregate.sample_count;
    aggregate.total_in_flight_bytes += state.in_flight_bytes;
    aggregate.retrans_count += state.retrans_count;
    aggregate.lost_count += state.lost_count;
    aggregate.mean_btl_bw_bps += state.btl_bw_bps.value();
    aggregate.ecn_mark_ppm = std::max(aggregate.ecn_mark_ppm,
                                      state.dctcp.ecn_mark_ppm);
    if (mode_worse(state.mode, aggregate.worst_mode)) {
        aggregate.worst_mode = state.mode;
    }
    const std::uint64_t rtt_value =
        std::min(state.rt_prop_us.value(), RttHistogram::max_trackable_value);
    const std::uint64_t bw_value =
        std::min(state.btl_bw_bps.value(), BandwidthHistogram::max_trackable_value);
    rtt_hist.record(RttHistogram::checked_value(rtt_value));
    bw_hist.record(BandwidthHistogram::checked_value(bw_value));
}

}  // namespace

std::string_view congestion_mode_name(CongestionMode mode) noexcept {
    switch (mode) {
        case CongestionMode::Open:        return "Open";
        case CongestionMode::Disorder:    return "Disorder";
        case CongestionMode::Cwr:         return "Cwr";
        case CongestionMode::Recovery:    return "Recovery";
        case CongestionMode::Loss:        return "Loss";
        case CongestionMode::BbrStartup:  return "BbrStartup";
        case CongestionMode::BbrDrain:    return "BbrDrain";
        case CongestionMode::BbrProbeBw:  return "BbrProbeBw";
        case CongestionMode::BbrProbeRtt: return "BbrProbeRtt";
        case CongestionMode::Unknown:     return "Unknown";
        default:                          return "<unknown CongestionMode>";
    }
}

std::string_view telemetry_error_name(TelemetryError error) noexcept {
    switch (error) {
        case TelemetryError::InvalidSocketFd:  return "InvalidSocketFd";
        case TelemetryError::InvalidNicCog:    return "InvalidNicCog";
        case TelemetryError::GetTcpInfoFailed: return "GetTcpInfoFailed";
        case TelemetryError::GetCcInfoFailed:  return "GetCcInfoFailed";
        case TelemetryError::EmptySampleSet:   return "EmptySampleSet";
        case TelemetryError::TooManyLinks:     return "TooManyLinks";
        case TelemetryError::LinkNotStarted:   return "LinkNotStarted";
        case TelemetryError::DeadlineOverflow: return "DeadlineOverflow";
        default:                               return "<unknown TelemetryError>";
    }
}

std::expected<TcpInfoSnapshot, TelemetryError>
harvest_socket(cntp::SocketFd fd) noexcept {
    tcp_info info{};
    socklen_t info_len = sizeof(info);
    const int tcp_rc = ::getsockopt(
        fd.value(), IPPROTO_TCP, TCP_INFO, &info, &info_len);
    if (tcp_rc != 0 || info_len < offsetof(tcp_info, tcpi_total_retrans)) {
        static_cast<void>(errno);
        return std::unexpected(TelemetryError::GetTcpInfoFailed);
    }

    cntp::CcAlgorithm algorithm = cntp::CcAlgorithm::Custom;
    if (auto selected = cntp::query_cc_for_socket(fd); selected.has_value()) {
        algorithm = *selected;
    }
    CongestionState state = decode_tcp_info(info, algorithm);

    tcp_cc_info cc_info{};
    socklen_t cc_len = sizeof(cc_info);
    const int cc_rc = ::getsockopt(
        fd.value(), IPPROTO_TCP, TCP_CC_INFO, &cc_info, &cc_len);
    if (cc_rc == 0) {
        if (algorithm == cntp::CcAlgorithm::Bbr1 ||
            algorithm == cntp::CcAlgorithm::Bbr2 ||
            algorithm == cntp::CcAlgorithm::Bbr3) {
            state.bbr = decode_bbr(cc_info.bbr);
            state.btl_bw_bps = state.bbr.btl_bw_bps;
            state.rt_prop_us = state.bbr.rt_prop_us;
            state.mode = mode_from_bbr(state.bbr);
            state.has_bbr = true;
        } else if (algorithm == cntp::CcAlgorithm::Dctcp) {
            state.dctcp = decode_dctcp(cc_info.dctcp);
            state.has_dctcp = true;
        }
    }

    return TcpInfoSnapshot{state};
}

CongestionAggregate
aggregate_congestion(cog::CogIdentity const& nic,
                     std::span<const TcpInfoSnapshot> samples) noexcept {
    CongestionAggregate aggregate{
        .nic_uuid = nic.uuid,
    };
    RttHistogram rtt_hist{};
    BandwidthHistogram bw_hist{};

    for (auto const& sample : samples) {
        record_sample(aggregate, rtt_hist, bw_hist, sample);
    }
    if (aggregate.sample_count != 0) {
        aggregate.mean_btl_bw_bps /= aggregate.sample_count;
        aggregate.p50_rtt_us = rtt_hist.percentile(50.0);
        aggregate.p95_rtt_us = rtt_hist.percentile(95.0);
        aggregate.p95_btl_bw_bps = bw_hist.percentile(95.0);
    }
    return aggregate;
}

std::expected<CongestionAggregate, TelemetryError>
harvest_per_link(cog::CogIdentity const& nic,
                 std::span<const cntp::SocketFd> active_fds) noexcept {
    if (!is_nic_cog(nic)) {
        return std::unexpected(TelemetryError::InvalidNicCog);
    }
    if (active_fds.empty()) {
        return std::unexpected(TelemetryError::EmptySampleSet);
    }

    CongestionAggregate aggregate{
        .nic_uuid = nic.uuid,
    };
    RttHistogram rtt_hist{};
    BandwidthHistogram bw_hist{};
    for (auto fd : active_fds) {
        auto sample = harvest_socket(fd);
        if (!sample.has_value()) {
            return std::unexpected(sample.error());
        }
        record_sample(aggregate, rtt_hist, bw_hist, *sample);
    }
    aggregate.mean_btl_bw_bps /= aggregate.sample_count;
    aggregate.p50_rtt_us = rtt_hist.percentile(50.0);
    aggregate.p95_rtt_us = rtt_hist.percentile(95.0);
    aggregate.p95_btl_bw_bps = bw_hist.percentile(95.0);
    return aggregate;
}

}  // namespace crucible::topology
