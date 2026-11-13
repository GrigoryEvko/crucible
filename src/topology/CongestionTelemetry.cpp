#include <crucible/topology/CongestionTelemetry.h>

#include <cstddef>
#include <cstdint>

namespace crucible::topology {

namespace {

namespace tcp_field = detail::tcp_info_field;
namespace bbr_field = detail::bbr_info_field;
namespace dctcp_field = detail::dctcp_info_field;

using TcpInfoBytes = ::fixy::View<std::byte const, detail::tcp_info_bytes>;
using BbrInfoBytes = ::fixy::View<std::byte const, detail::bbr_info_bytes>;
using DctcpInfoBytes = ::fixy::View<std::byte const, detail::dctcp_info_bytes>;

// The value of the field Field of a kernel struct, from the bytes of the
// struct.  The constraint puts the field inside the bytes.  decode gives no
// value for an enumeration whose byte names no enumerator.
template <auto Field, std::size_t Extent>
    requires(Field.offset + Field.size <= Extent)
[[nodiscard]] ::fixy::Option<typename decltype(Field)::value_type>
decode_field(::fixy::View<std::byte const, Extent> bytes) noexcept {
    using Value = typename decltype(Field)::value_type;
    ::foundation::core::FixedArray<std::byte, sizeof(Value)> word{};
    ::fixy::copy(
        word.view(),
        bytes.template window<sizeof(Value)>(Field.offset).expect("the constraint puts the field inside the bytes"));
    return ::fixy::decode<Value>(word);
}

// The value of an integer field.  Each bit pattern of an integer is a value,
// so decode gives a value for each bytes.
template <auto Field, std::size_t Extent>
    requires(Field.offset + Field.size <= Extent) && ::fixy::UnsignedInteger<typename decltype(Field)::value_type>
[[nodiscard]] typename decltype(Field)::value_type decode_word(::fixy::View<std::byte const, Extent> bytes) noexcept {
    return decode_field<Field>(bytes).expect("each bit pattern of an integer is a value");
}

[[nodiscard]] constexpr std::uint64_t positive_or_one_u64(std::uint64_t v) noexcept {
    return v == 0 ? std::uint64_t{1} : v;
}

[[nodiscard]] constexpr std::uint32_t positive_or_one_u32(std::uint32_t v) noexcept {
    return v == 0 ? std::uint32_t{1} : v;
}

[[nodiscard]] constexpr std::uint32_t sat_u32_mul(std::uint32_t a, std::uint32_t b) noexcept {
    const std::uint64_t product = static_cast<std::uint64_t>(a) * b;
    return product > ::fixy::max_of<std::uint32_t> ? ::fixy::max_of<std::uint32_t>
                                                   : static_cast<std::uint32_t>(product);
}

[[nodiscard]] constexpr CongestionMode mode_from_ca_state(detail::TcpCaState state) noexcept {
    switch (state) {
        case detail::TcpCaState::Open:
            return CongestionMode::Open;
        case detail::TcpCaState::Disorder:
            return CongestionMode::Disorder;
        case detail::TcpCaState::Cwr:
            return CongestionMode::Cwr;
        case detail::TcpCaState::Recovery:
            return CongestionMode::Recovery;
        case detail::TcpCaState::Loss:
            return CongestionMode::Loss;
        default:
            return CongestionMode::Unknown;
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
        case CongestionMode::Open:
            return 0;
        case CongestionMode::BbrProbeBw:
            return 1;
        case CongestionMode::BbrStartup:
            return 1;
        case CongestionMode::BbrProbeRtt:
            return 2;
        case CongestionMode::BbrDrain:
            return 2;
        case CongestionMode::Disorder:
            return 3;
        case CongestionMode::Cwr:
            return 4;
        case CongestionMode::Recovery:
            return 5;
        case CongestionMode::Loss:
            return 6;
        case CongestionMode::Unknown:
            return 1;
        default:
            return 1;
    }
}

[[nodiscard]] constexpr bool mode_worse(CongestionMode lhs, CongestionMode rhs) noexcept {
    return mode_severity(lhs) > mode_severity(rhs);
}

[[nodiscard]] constexpr bool is_bbr(cntp::CcAlgorithm algorithm) noexcept {
    return algorithm == cntp::CcAlgorithm::Bbr1 || algorithm == cntp::CcAlgorithm::Bbr2
        || algorithm == cntp::CcAlgorithm::Bbr3;
}

// The two decoders below mint each refined field from positive_or_one_*,
// which maps zero to one, so the checked mint never fires.
[[nodiscard]] BbrFields decode_bbr(BbrInfoBytes info) noexcept {
    const std::uint64_t bw =
        (static_cast<std::uint64_t>(decode_word<bbr_field::bw_hi>(info)) << 32u) | decode_word<bbr_field::bw_lo>(info);
    return BbrFields{
        .btl_bw_bps = ::fixy::mint_refined<::fixy::positive>(positive_or_one_u64(bw)),
        .rt_prop_us = ::fixy::mint_refined<::fixy::positive>(
            std::uint64_t{positive_or_one_u32(decode_word<bbr_field::min_rtt>(info))}),
        .pacing_gain_q8 = decode_word<bbr_field::pacing_gain>(info),
        .cwnd_gain_q8 = decode_word<bbr_field::cwnd_gain>(info),
    };
}

[[nodiscard]] DctcpFields decode_dctcp(DctcpInfoBytes info) noexcept {
    std::uint32_t const ab_ecn = decode_word<dctcp_field::ab_ecn>(info);
    std::uint32_t const ab_tot = decode_word<dctcp_field::ab_tot>(info);
    std::uint32_t ppm = 0;
    if (ab_tot != 0) {
        const std::uint64_t scaled = (static_cast<std::uint64_t>(ab_ecn) * 1000000ull) / ab_tot;
        ppm = static_cast<std::uint32_t>(::fixy::min(scaled, std::uint64_t{1000000}));
    }
    return DctcpFields{
        .alpha_q10 = decode_word<dctcp_field::alpha>(info),
        .ecn_mark_ppm = ppm,
        .enabled = decode_word<dctcp_field::enabled>(info) != 0,
    };
}

// A congestion state that names no enumerator of the kernel gives the mode
// Unknown.
[[nodiscard]] CongestionSample decode_tcp_info(TcpInfoBytes info, cntp::CcAlgorithm algorithm) noexcept {
    const std::uint32_t snd_cwnd = decode_word<tcp_field::snd_cwnd>(info);
    const std::uint32_t snd_ssthresh = decode_word<tcp_field::snd_ssthresh>(info);
    const std::uint32_t min_rtt = decode_word<tcp_field::min_rtt>(info);
    const std::uint32_t mss = positive_or_one_u32(decode_word<tcp_field::snd_mss>(info));
    const std::uint32_t cwnd_bytes = sat_u32_mul(positive_or_one_u32(snd_cwnd), mss);
    const std::uint32_t ssthresh_packets =
        snd_ssthresh == ::fixy::max_of<std::uint32_t> ? snd_cwnd : positive_or_one_u32(snd_ssthresh);
    const std::uint32_t ssthresh_bytes = sat_u32_mul(ssthresh_packets, mss);
    const std::uint32_t in_flight = sat_u32_mul(decode_word<tcp_field::unacked>(info), mss);
    const std::uint64_t delivery_rate = positive_or_one_u64(decode_word<tcp_field::delivery_rate>(info));
    const std::uint64_t rtt = positive_or_one_u64(min_rtt != 0 ? min_rtt : decode_word<tcp_field::rtt>(info));

    CongestionSample state{
        .algorithm = algorithm,
        .btl_bw_bps = ::fixy::mint_refined<::fixy::positive>(delivery_rate),
        .rt_prop_us = ::fixy::mint_refined<::fixy::positive>(rtt),
        .cwnd_bytes = ::fixy::mint_refined<::fixy::positive>(positive_or_one_u32(cwnd_bytes)),
        .ssthresh_bytes = ::fixy::mint_refined<::fixy::positive>(positive_or_one_u32(ssthresh_bytes)),
        .retrans_count = decode_word<tcp_field::total_retrans>(info),
        .lost_count = decode_word<tcp_field::lost>(info),
        .in_flight_bytes = in_flight,
        .pacing_rate_bps = decode_word<tcp_field::pacing_rate>(info),
        .max_pacing_rate_bps = decode_word<tcp_field::max_pacing_rate>(info),
        .delivered = decode_word<tcp_field::delivered>(info),
        .delivered_ce = decode_word<tcp_field::delivered_ce>(info),
        .mode = decode_field<tcp_field::ca_state>(info).match(
            [](detail::TcpCaState ca_state) noexcept { return mode_from_ca_state(ca_state); },
            []() noexcept { return CongestionMode::Unknown; }),
    };

    if (state.delivered != 0) {
        const std::uint64_t scaled = (static_cast<std::uint64_t>(state.delivered_ce) * 1000000ull) / state.delivered;
        state.dctcp.ecn_mark_ppm = static_cast<std::uint32_t>(::fixy::min(scaled, std::uint64_t{1000000}));
    }
    return state;
}

// The sample with the fields of BBR or of DCTCP from the bytes of
// TCP_CC_INFO.  When the kernel wrote too few bytes for the controller of
// the sample, the sample stays as it is.
[[nodiscard]] CongestionSample add_congestion_info(CongestionSample state,
                                                   ::fixy::View<std::byte const> congestion_info) noexcept {
    if (is_bbr(state.algorithm)) {
        return congestion_info.window<detail::bbr_info_bytes>(0).match(
            [state](BbrInfoBytes bytes) noexcept {
                CongestionSample with_bbr = state;
                with_bbr.bbr = decode_bbr(bytes);
                with_bbr.btl_bw_bps = with_bbr.bbr.btl_bw_bps;
                with_bbr.rt_prop_us = with_bbr.bbr.rt_prop_us;
                with_bbr.mode = mode_from_bbr(with_bbr.bbr);
                with_bbr.has_bbr = true;
                return with_bbr;
            },
            [state]() noexcept { return state; });
    }
    if (state.algorithm == cntp::CcAlgorithm::Dctcp) {
        return congestion_info.window<detail::dctcp_info_bytes>(0).match(
            [state](DctcpInfoBytes bytes) noexcept {
                CongestionSample with_dctcp = state;
                with_dctcp.dctcp = decode_dctcp(bytes);
                with_dctcp.has_dctcp = true;
                return with_dctcp;
            },
            [state]() noexcept { return state; });
    }
    return state;
}

}  // namespace

::fixy::Result<TcpInfoSnapshot, TelemetryError> detail::decode_tcp_sample(::fixy::View<std::byte const> info,
                                                                          ::fixy::View<std::byte const> congestion_info,
                                                                          cntp::CcAlgorithm algorithm) noexcept {
    using Decoded = ::fixy::Result<TcpInfoSnapshot, TelemetryError>;
    return info.window<tcp_info_bytes>(0).match(
        [congestion_info, algorithm](TcpInfoBytes fields) noexcept -> Decoded {
            CongestionSample const state = add_congestion_info(decode_tcp_info(fields, algorithm), congestion_info);
            return ::fixy::mint_tagged<::fixy::tags::source::TcpInfo>(state);
        },
        []() noexcept -> Decoded { return ::fixy::err(TelemetryError::GetTcpInfoFailed); });
}

void detail::CongestionAccumulator::record(TcpInfoSnapshot const& snapshot) noexcept {
    auto const& state = snapshot.value();
    ++aggregate_.sample_count;
    aggregate_.total_in_flight_bytes += state.in_flight_bytes;
    aggregate_.retrans_count += state.retrans_count;
    aggregate_.lost_count += state.lost_count;
    aggregate_.mean_btl_bw_bps += state.btl_bw_bps.value();
    aggregate_.ecn_mark_ppm = ::fixy::max(aggregate_.ecn_mark_ppm, state.dctcp.ecn_mark_ppm);
    if (mode_worse(state.mode, aggregate_.worst_mode)) {
        aggregate_.worst_mode = state.mode;
    }
    const std::uint64_t rtt_value = ::fixy::min(state.rt_prop_us.value(), RttHistogram::max_trackable_value);
    const std::uint64_t bw_value = ::fixy::min(state.btl_bw_bps.value(), BandwidthHistogram::max_trackable_value);
    rtt_histogram_.record(RttHistogram::checked_value(rtt_value));
    bandwidth_histogram_.record(BandwidthHistogram::checked_value(bw_value));
}

CongestionAggregate detail::CongestionAccumulator::finish() const noexcept {
    CongestionAggregate aggregate = aggregate_;
    if (aggregate.sample_count == 0) {
        return aggregate;
    }
    aggregate.mean_btl_bw_bps /= aggregate.sample_count;
    aggregate.p50_rtt_us = rtt_histogram_.percentile(50.0);
    aggregate.p95_rtt_us = rtt_histogram_.percentile(95.0);
    aggregate.p95_btl_bw_bps = bandwidth_histogram_.percentile(95.0);
    return aggregate;
}

CongestionAggregate aggregate_congestion(cog::CogIdentity const& nic,
                                         ::fixy::View<TcpInfoSnapshot const> samples) noexcept {
    detail::CongestionAccumulator accumulator{nic};
    for (TcpInfoSnapshot const& sample : samples) {
        accumulator.record(sample);
    }
    return accumulator.finish();
}

}  // namespace crucible::topology
