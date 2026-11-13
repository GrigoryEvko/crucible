#pragma once

#include <crucible/cntp/CongestionControl.h>
#include <crucible/cog/CogIdentity.h>
#include <crucible/observe/HdrHistogram.h>
#include <fixy/Core.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <fixy/os/Socket.h>
#include <foundation/Platform.h>
#include <foundation/effects/Ctx.h>

#include <cstddef>
#include <cstdint>
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

[[nodiscard]] constexpr ::fixy::Result<PositiveSamplePeriodNs, TelemetryError>
admit_sample_period_ns(std::uint64_t ns) noexcept {
    return ::fixy::admit_refined<::fixy::positive>(ns, TelemetryError::DeadlineOverflow);
}

[[nodiscard]] constexpr bool is_nic_cog(cog::CogIdentity const& identity) noexcept {
    return identity.kind == cog::CogKind::NicPort || identity.kind == cog::CogKind::NicCard;
}

[[nodiscard]] CongestionAggregate aggregate_congestion(cog::CogIdentity const& nic,
                                                       ::fixy::View<TcpInfoSnapshot const> samples) noexcept;

namespace detail {

// One field of a kernel struct: the type of its value, its size and its
// offset in bytes from the start of the struct.
template <class T>
struct KernelField {
    using value_type = T;
    static constexpr std::size_t size = sizeof(T);
    std::size_t offset = 0;
};

// The congestion state of a TCP socket in the kernel: the enum
// tcp_ca_state of <linux/tcp.h>.
enum class TcpCaState : std::uint8_t {
    Open = 0,
    Disorder = 1,
    Cwr = 2,
    Recovery = 3,
    Loss = 4,
};

// The fields of the kernel struct tcp_info of <linux/tcp.h> that a harvest
// reads.  The test of the harvest compares each offset and each size with
// the kernel header.
namespace tcp_info_field {
inline constexpr KernelField<TcpCaState> ca_state{.offset = 1};
inline constexpr KernelField<std::uint32_t> snd_mss{.offset = 16};
inline constexpr KernelField<std::uint32_t> unacked{.offset = 24};
inline constexpr KernelField<std::uint32_t> lost{.offset = 32};
inline constexpr KernelField<std::uint32_t> rtt{.offset = 68};
inline constexpr KernelField<std::uint32_t> snd_ssthresh{.offset = 76};
inline constexpr KernelField<std::uint32_t> snd_cwnd{.offset = 80};
inline constexpr KernelField<std::uint32_t> total_retrans{.offset = 100};
inline constexpr KernelField<std::uint64_t> pacing_rate{.offset = 104};
inline constexpr KernelField<std::uint64_t> max_pacing_rate{.offset = 112};
inline constexpr KernelField<std::uint32_t> min_rtt{.offset = 148};
inline constexpr KernelField<std::uint64_t> delivery_rate{.offset = 160};
inline constexpr KernelField<std::uint32_t> delivered{.offset = 192};
inline constexpr KernelField<std::uint32_t> delivered_ce{.offset = 196};
}  // namespace tcp_info_field

// The fields of the kernel struct tcp_bbr_info of <linux/inet_diag.h>.  It
// is the member bbr of the union tcp_cc_info, at the start of the union.
namespace bbr_info_field {
inline constexpr KernelField<std::uint32_t> bw_lo{.offset = 0};
inline constexpr KernelField<std::uint32_t> bw_hi{.offset = 4};
inline constexpr KernelField<std::uint32_t> min_rtt{.offset = 8};
inline constexpr KernelField<std::uint32_t> pacing_gain{.offset = 12};
inline constexpr KernelField<std::uint32_t> cwnd_gain{.offset = 16};
}  // namespace bbr_info_field

// The fields of the kernel struct tcp_dctcp_info of <linux/inet_diag.h>.
// It is the member dctcp of the union tcp_cc_info, at the start of the union.
namespace dctcp_info_field {
inline constexpr KernelField<std::uint16_t> enabled{.offset = 0};
inline constexpr KernelField<std::uint32_t> alpha{.offset = 4};
inline constexpr KernelField<std::uint32_t> ab_ecn{.offset = 8};
inline constexpr KernelField<std::uint32_t> ab_tot{.offset = 12};
}  // namespace dctcp_info_field

// The bytes of tcp_info that a harvest reads: the struct up to the end of
// delivered_ce, its last field.  An older kernel writes a shorter struct,
// and the harvest then gives GetTcpInfoFailed.
inline constexpr std::size_t tcp_info_bytes = tcp_info_field::delivered_ce.offset + tcp_info_field::delivered_ce.size;

// The bytes of tcp_bbr_info and of tcp_dctcp_info up to the end of the last
// field that a harvest reads.
inline constexpr std::size_t bbr_info_bytes = bbr_info_field::cwnd_gain.offset + bbr_info_field::cwnd_gain.size;
inline constexpr std::size_t dctcp_info_bytes = dctcp_info_field::ab_tot.offset + dctcp_info_field::ab_tot.size;

// The size of the union tcp_cc_info: the size of its largest member,
// tcp_bbr_info.
inline constexpr std::size_t cc_info_bytes = bbr_info_bytes;

// The sample of the bytes that the kernel wrote for TCP_INFO (info) and
// for TCP_CC_INFO (congestion_info).  An info of fewer than tcp_info_bytes
// gives GetTcpInfoFailed.  The fields of BBR or of DCTCP come from
// congestion_info only when algorithm names that controller and the kernel
// wrote each byte of those fields.
[[nodiscard]] ::fixy::Result<TcpInfoSnapshot, TelemetryError>
decode_tcp_sample(::fixy::View<std::byte const> info, ::fixy::View<std::byte const> congestion_info,
                  cntp::CcAlgorithm algorithm) noexcept;

// The first count bytes of buffer: the bytes that the kernel wrote.  The
// kernel writes at most the size of the buffer, so a larger count does not
// occur, and it gives no bytes.
template <std::size_t Size>
[[nodiscard]] constexpr ::fixy::View<std::byte const>
written_bytes(::foundation::core::FixedArray<std::byte, Size> const& buffer, std::size_t count) noexcept {
    return ::fixy::View<std::byte const>{buffer.view()}.window(0, count).value_or(::fixy::View<std::byte const>{});
}

// The running aggregate of the samples of one NIC: the sums, the worst mode
// and the two histograms of the percentiles.  The two histograms hold some
// tens of kilobytes, so an accumulator lives on the stack of one
// aggregation.
class CongestionAccumulator {
public:
    explicit CongestionAccumulator(cog::CogIdentity const& nic) noexcept : aggregate_{.nic_uuid = nic.uuid} {}

    CongestionAccumulator(CongestionAccumulator const&) = delete("an accumulator holds one aggregation");
    CongestionAccumulator& operator=(CongestionAccumulator const&) = delete("an accumulator holds one aggregation");
    CongestionAccumulator(CongestionAccumulator&&) = delete("an accumulator holds one aggregation");
    CongestionAccumulator& operator=(CongestionAccumulator&&) = delete("an accumulator holds one aggregation");
    ~CongestionAccumulator() = default;

    void record(TcpInfoSnapshot const& snapshot) noexcept;

    // The aggregate of the recorded samples.  With no sample, each mean and
    // each percentile stays zero.
    [[nodiscard]] CongestionAggregate finish() const noexcept;

private:
    using RttHistogram = observe::HdrHistogram<2, 3600000000000ull>;
    using BandwidthHistogram = observe::HdrHistogram<2, 1000000000000ull>;

    CongestionAggregate aggregate_{};
    RttHistogram rtt_histogram_{};
    BandwidthHistogram bandwidth_histogram_{};
};

}  // namespace detail

// A harvest reads TCP_INFO and TCP_CC_INFO through the socket door, and
// TCP_CONGESTION through cntp.  Each read is a socket option call, so the
// harvest takes a context that admits the socket option row.  A failed read of TCP_INFO writes no byte, and the decode then gives
// GetTcpInfoFailed.  A failed read of TCP_CC_INFO gives a sample with no
// fields of BBR or of DCTCP.
template <::foundation::effects::IsExecCtx Ctx>
    requires cntp::CtxFitsSocketOption<Ctx>
[[nodiscard]] ::fixy::Result<TcpInfoSnapshot, TelemetryError> harvest_socket(Ctx const& ctx,
                                                                             cntp::SocketFd fd) noexcept {
    ::foundation::core::FixedArray<std::byte, detail::tcp_info_bytes> info{};
    std::size_t const info_count =
        ::fixy::net::read_tcp_option(ctx, fd, ::fixy::net::TcpOption::info, info.view()).value_or(0);
    cntp::CcAlgorithm const algorithm = cntp::query_cc_selection_for_socket(ctx, fd).match(
        [](cntp::CcSelection selected) noexcept { return selected.algorithm; },
        [](cntp::CcError) noexcept { return cntp::CcAlgorithm::Custom; });
    ::foundation::core::FixedArray<std::byte, detail::cc_info_bytes> congestion_info{};
    std::size_t const congestion_count =
        ::fixy::net::read_tcp_option(ctx, fd, ::fixy::net::TcpOption::congestion_info, congestion_info.view())
            .value_or(0);
    return detail::decode_tcp_sample(detail::written_bytes(info, info_count),
                                     detail::written_bytes(congestion_info, congestion_count), algorithm);
}

// The aggregate of one harvest of each socket of a NIC.  The first failed
// harvest ends the call with its error.  Linear in the sockets.
template <::foundation::effects::IsExecCtx Ctx>
    requires cntp::CtxFitsSocketOption<Ctx>
[[nodiscard]] ::fixy::Result<CongestionAggregate, TelemetryError>
harvest_per_link(Ctx const& ctx, cog::CogIdentity const& nic, ::fixy::View<cntp::SocketFd const> active_fds) noexcept {
    if (!is_nic_cog(nic)) {
        return ::fixy::err(TelemetryError::InvalidNicCog);
    }
    if (active_fds.size() == 0) {
        return ::fixy::err(TelemetryError::EmptySampleSet);
    }
    detail::CongestionAccumulator accumulator{nic};
    for (cntp::SocketFd const fd : active_fds) {
        CRUCIBLE_TRY(sample, harvest_socket(ctx, fd));
        accumulator.record(sample);
    }
    return accumulator.finish();
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
