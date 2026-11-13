#include <crucible/topology/CongestionTelemetryWorker.h>
#include <crucible/topology/CongestionTelemetry.h>
#include <fixy/os/Socket.h>
#include <foundation/reflect/EnumName.h>

#include "test_assert.h"

#include <concepts>
#include <cstddef>
#include <cstdio>
#include <string_view>
#include <utility>
#include <type_traits>

#include <linux/inet_diag.h>
#include <linux/tcp.h>

namespace cntp = crucible::cntp;
namespace cog = crucible::cog;
namespace eff = ::fixy;
namespace observe = crucible::observe;
namespace topology = crucible::topology;

namespace {

namespace layout = topology::detail;

// The offset and the size of a field of the decode agree with the field of
// the kernel struct.
template <class T>
consteval bool field_matches(layout::KernelField<T> field, std::size_t offset, std::size_t size) {
    return field.offset == offset && field.size == size;
}

// The fields that the harvest decodes are the fields of the kernel structs
// of <linux/tcp.h> and <linux/inet_diag.h>.
static_assert(field_matches(layout::tcp_info_field::ca_state, offsetof(tcp_info, tcpi_ca_state),
                            sizeof(tcp_info::tcpi_ca_state)));
static_assert(field_matches(layout::tcp_info_field::snd_mss, offsetof(tcp_info, tcpi_snd_mss),
                            sizeof(tcp_info::tcpi_snd_mss)));
static_assert(field_matches(layout::tcp_info_field::unacked, offsetof(tcp_info, tcpi_unacked),
                            sizeof(tcp_info::tcpi_unacked)));
static_assert(field_matches(layout::tcp_info_field::lost, offsetof(tcp_info, tcpi_lost), sizeof(tcp_info::tcpi_lost)));
static_assert(field_matches(layout::tcp_info_field::rtt, offsetof(tcp_info, tcpi_rtt), sizeof(tcp_info::tcpi_rtt)));
static_assert(field_matches(layout::tcp_info_field::snd_ssthresh, offsetof(tcp_info, tcpi_snd_ssthresh),
                            sizeof(tcp_info::tcpi_snd_ssthresh)));
static_assert(field_matches(layout::tcp_info_field::snd_cwnd, offsetof(tcp_info, tcpi_snd_cwnd),
                            sizeof(tcp_info::tcpi_snd_cwnd)));
static_assert(field_matches(layout::tcp_info_field::total_retrans, offsetof(tcp_info, tcpi_total_retrans),
                            sizeof(tcp_info::tcpi_total_retrans)));
static_assert(field_matches(layout::tcp_info_field::pacing_rate, offsetof(tcp_info, tcpi_pacing_rate),
                            sizeof(tcp_info::tcpi_pacing_rate)));
static_assert(field_matches(layout::tcp_info_field::max_pacing_rate, offsetof(tcp_info, tcpi_max_pacing_rate),
                            sizeof(tcp_info::tcpi_max_pacing_rate)));
static_assert(field_matches(layout::tcp_info_field::min_rtt, offsetof(tcp_info, tcpi_min_rtt),
                            sizeof(tcp_info::tcpi_min_rtt)));
static_assert(field_matches(layout::tcp_info_field::delivery_rate, offsetof(tcp_info, tcpi_delivery_rate),
                            sizeof(tcp_info::tcpi_delivery_rate)));
static_assert(field_matches(layout::tcp_info_field::delivered, offsetof(tcp_info, tcpi_delivered),
                            sizeof(tcp_info::tcpi_delivered)));
static_assert(field_matches(layout::tcp_info_field::delivered_ce, offsetof(tcp_info, tcpi_delivered_ce),
                            sizeof(tcp_info::tcpi_delivered_ce)));
static_assert(layout::tcp_info_bytes <= sizeof(tcp_info));

static_assert(offsetof(tcp_cc_info, bbr) == 0 && offsetof(tcp_cc_info, dctcp) == 0);
static_assert(layout::cc_info_bytes == sizeof(tcp_cc_info) && layout::bbr_info_bytes == sizeof(tcp_bbr_info));
static_assert(layout::dctcp_info_bytes == sizeof(tcp_dctcp_info));
static_assert(field_matches(layout::bbr_info_field::bw_lo, offsetof(tcp_bbr_info, bbr_bw_lo),
                            sizeof(tcp_bbr_info::bbr_bw_lo)));
static_assert(field_matches(layout::bbr_info_field::bw_hi, offsetof(tcp_bbr_info, bbr_bw_hi),
                            sizeof(tcp_bbr_info::bbr_bw_hi)));
static_assert(field_matches(layout::bbr_info_field::min_rtt, offsetof(tcp_bbr_info, bbr_min_rtt),
                            sizeof(tcp_bbr_info::bbr_min_rtt)));
static_assert(field_matches(layout::bbr_info_field::pacing_gain, offsetof(tcp_bbr_info, bbr_pacing_gain),
                            sizeof(tcp_bbr_info::bbr_pacing_gain)));
static_assert(field_matches(layout::bbr_info_field::cwnd_gain, offsetof(tcp_bbr_info, bbr_cwnd_gain),
                            sizeof(tcp_bbr_info::bbr_cwnd_gain)));
static_assert(field_matches(layout::dctcp_info_field::enabled, offsetof(tcp_dctcp_info, dctcp_enabled),
                            sizeof(tcp_dctcp_info::dctcp_enabled)));
static_assert(field_matches(layout::dctcp_info_field::alpha, offsetof(tcp_dctcp_info, dctcp_alpha),
                            sizeof(tcp_dctcp_info::dctcp_alpha)));
static_assert(field_matches(layout::dctcp_info_field::ab_ecn, offsetof(tcp_dctcp_info, dctcp_ab_ecn),
                            sizeof(tcp_dctcp_info::dctcp_ab_ecn)));
static_assert(field_matches(layout::dctcp_info_field::ab_tot, offsetof(tcp_dctcp_info, dctcp_ab_tot),
                            sizeof(tcp_dctcp_info::dctcp_ab_tot)));

static_assert(static_cast<int>(layout::TcpCaState::Open) == TCP_CA_Open);
static_assert(static_cast<int>(layout::TcpCaState::Disorder) == TCP_CA_Disorder);
static_assert(static_cast<int>(layout::TcpCaState::Cwr) == TCP_CA_CWR);
static_assert(static_cast<int>(layout::TcpCaState::Recovery) == TCP_CA_Recovery);
static_assert(static_cast<int>(layout::TcpCaState::Loss) == TCP_CA_Loss);

using InfoBytes = ::foundation::core::FixedArray<std::byte, layout::tcp_info_bytes>;
using CongestionInfoBytes = ::foundation::core::FixedArray<std::byte, layout::cc_info_bytes>;

// Writes value into the bytes of a kernel struct, at the offset of field.
template <class T, std::size_t Size>
void put_field(::foundation::core::FixedArray<std::byte, Size>& bytes, layout::KernelField<T> field, T value) {
    ::fixy::copy(bytes.view().template window<sizeof(T)>(field.offset).expect("the field lies inside the struct"),
                 ::fixy::bytes(value));
}

// The descriptor of a mint, or an empty handle when the open failed.
[[nodiscard]] ::fixy::fs::OwnedFd
descriptor_of(::fixy::Result<::fixy::Linear<::fixy::fs::OwnedFd>, ::foundation::os::Errno> minted) noexcept {
    return std::move(minted).match(
        [](::fixy::Linear<::fixy::fs::OwnedFd> opened) -> ::fixy::fs::OwnedFd { return std::move(opened).consume(); },
        [](::foundation::os::Errno) -> ::fixy::fs::OwnedFd { return ::fixy::fs::OwnedFd{}; });
}

cog::CogIdentity nic(std::uint64_t lo) {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{0x1234, lo};
    id.level = cog::CogLevel::L0_Atomic;
    id.kind = cog::CogKind::NicPort;
    return id;
}

cog::CogIdentity gpu(std::uint64_t lo) {
    cog::CogIdentity id = nic(lo);
    id.kind = cog::CogKind::Gpu;
    return id;
}

topology::CongestionSample state(std::uint64_t bw, std::uint64_t rtt, topology::CongestionMode mode) {
    topology::CongestionSample s{};
    s.algorithm = cntp::CcAlgorithm::Bbr3;
    s.btl_bw_bps = ::fixy::mint_refined<::fixy::positive>(bw);
    s.rt_prop_us = ::fixy::mint_refined<::fixy::positive>(rtt);
    s.cwnd_bytes = ::fixy::mint_refined<::fixy::positive>(std::uint32_t{65536});
    s.ssthresh_bytes = ::fixy::mint_refined<::fixy::positive>(std::uint32_t{131072});
    s.in_flight_bytes = 32768;
    s.mode = mode;
    s.has_bbr = true;
    s.bbr.btl_bw_bps = s.btl_bw_bps;
    s.bbr.rt_prop_us = s.rt_prop_us;
    return s;
}

// The test stands in for the kernel, so it names the TCP_INFO source
// itself.  harvest_socket is the only producer in the library.
topology::TcpInfoSnapshot sample(std::uint64_t bw, std::uint64_t rtt, topology::CongestionMode mode) {
    return ::fixy::mint_tagged<::fixy::tags::source::TcpInfo>(state(bw, rtt, mode));
}

// The log spelling of both enums comes from reflection.
void test_names_and_admission() {
    static_assert(::foundation::reflect::enum_name(topology::CongestionMode::BbrDrain) == "BbrDrain");
    static_assert(::foundation::reflect::enum_name(topology::TelemetryError::InvalidNicCog) == "InvalidNicCog");
    volatile auto mode = topology::CongestionMode::BbrProbeRtt;
    assert(::foundation::reflect::enum_name(static_cast<topology::CongestionMode>(mode))
           == std::string_view{"BbrProbeRtt"});
    auto zero = topology::admit_sample_period_ns(0);
    assert(zero.is_err());
    assert(zero.err() == ::fixy::some(topology::TelemetryError::DeadlineOverflow));
    auto period = topology::admit_sample_period_ns(1000);
    assert(period.is_ok());
    assert(std::move(period).expect("1000 ns is positive").value() == 1000);
    crucible::test::pass("  test_names_and_admission: PASSED\n");
}

void test_aggregate_and_drift() {
    ::foundation::core::FixedArray<topology::TcpInfoSnapshot, 4> const samples{
        sample(1000000000, 100, topology::CongestionMode::Open),
        sample(800000000, 120, topology::CongestionMode::BbrProbeBw),
        sample(600000000, 150, topology::CongestionMode::Recovery),
        sample(500000000, 180, topology::CongestionMode::Loss),
    };

    auto aggregate = topology::aggregate_congestion(nic(1), samples.view());
    assert(aggregate.nic_uuid == nic(1).uuid);
    assert(aggregate.sample_count == samples.size());
    assert(aggregate.p95_rtt_us >= 150);
    assert(aggregate.p95_btl_bw_bps >= 800000000);
    assert(aggregate.mean_btl_bw_bps == 725000000);
    assert(aggregate.worst_mode == topology::CongestionMode::Loss);

    auto baseline = ::fixy::mint_refined<::fixy::positive>(std::uint64_t{1200000000});
    auto drift = topology::detect_congestion_drift(aggregate, baseline,
                                                   topology::CongestionDriftPolicy{
                                                       .bandwidth_drop_ppm = 100000,
                                                       .min_samples = 4,
                                                   });
    assert(drift.degraded);
    assert(drift.bandwidth_drop_ppm >= 100000);
    crucible::test::pass("  test_aggregate_and_drift: PASSED\n");
}

void test_worker_recording() {
    eff::ColdInitCtx init{::foundation::effects::testing::init()};
    eff::BgDrainCtx bg{::foundation::effects::testing::bg()};
    auto worker = topology::mint_congestion_telemetry_worker<2, 8>(init);

    ::foundation::core::FixedArray<cog::CogIdentity, 2> const nics{nic(10), nic(11)};
    cog::CogIdentity const& first_nic = nics.at(::fixy::index<0>());
    auto start = worker.start(init, nics.view());
    assert(start.is_ok());

    ::foundation::core::FixedArray<topology::TcpInfoSnapshot, 2> const samples{
        sample(900000000, 90, topology::CongestionMode::Open),
        sample(700000000, 110, topology::CongestionMode::Cwr),
    };
    auto const recorded =
        worker.record_link(bg, first_nic, samples.view(), 1).expect("the worker started the link of the first nic");
    assert(recorded.sample_count == samples.size());
    auto const last = worker.last(first_nic).expect("the worker recorded the link of the first nic");
    assert(last.sample_count == samples.size());

    auto unstarted = worker.last(nic(99));
    assert(unstarted.is_err());
    assert(unstarted.err().expect("the link did not start") == topology::TelemetryError::LinkNotStarted);

    ::foundation::core::FixedArray<cog::CogIdentity, 1> const bad_nics{gpu(1)};
    auto bad_start = worker.start(init, bad_nics.view());
    assert(bad_start.is_err());
    assert(bad_start.err().expect("a gpu is no nic") == topology::TelemetryError::InvalidNicCog);

    topology::CongestionObservationSet sinks{};
    topology::publish_congestion(sinks, 0, last, 7);
    auto p95 = observe::latest_observation(
        *sinks.get(static_cast<std::size_t>(topology::CongestionMetricSlot::P95BandwidthBps))
             .expect("the set holds one sink for each slot"));
    assert(p95.metric_id == topology::congestion_metric_id(0, topology::CongestionMetricSlot::P95BandwidthBps));
    assert(p95.sequence == 7);
    crucible::test::pass("  test_worker_recording: PASSED\n");
}

// The decode reads each field at its offset in the bytes of the kernel.  A
// short struct gives no sample, and a short union of the controller gives
// no fields of the controller.
void test_decode_tcp_sample() {
    InfoBytes info{};
    put_field(info, layout::tcp_info_field::ca_state, layout::TcpCaState::Recovery);
    put_field(info, layout::tcp_info_field::snd_mss, std::uint32_t{1000});
    put_field(info, layout::tcp_info_field::unacked, std::uint32_t{5});
    put_field(info, layout::tcp_info_field::lost, std::uint32_t{2});
    put_field(info, layout::tcp_info_field::rtt, std::uint32_t{300});
    put_field(info, layout::tcp_info_field::snd_ssthresh, ::fixy::max_of<std::uint32_t>);
    put_field(info, layout::tcp_info_field::snd_cwnd, std::uint32_t{20});
    put_field(info, layout::tcp_info_field::total_retrans, std::uint32_t{7});
    put_field(info, layout::tcp_info_field::pacing_rate, std::uint64_t{111});
    put_field(info, layout::tcp_info_field::max_pacing_rate, std::uint64_t{222});
    put_field(info, layout::tcp_info_field::delivery_rate, std::uint64_t{5000});
    put_field(info, layout::tcp_info_field::delivered, std::uint32_t{1000});
    put_field(info, layout::tcp_info_field::delivered_ce, std::uint32_t{250});
    ::fixy::View<std::byte const> const no_bytes{};

    topology::TcpInfoSnapshot const cubic =
        layout::decode_tcp_sample(info.view(), no_bytes, cntp::CcAlgorithm::Cubic).expect("the struct is full");
    assert(cubic.value().mode == topology::CongestionMode::Recovery);
    assert(cubic.value().cwnd_bytes.value() == 20000);
    assert(cubic.value().ssthresh_bytes.value() == 20000);
    assert(cubic.value().in_flight_bytes == 5000);
    assert(cubic.value().rt_prop_us.value() == 300);
    assert(cubic.value().btl_bw_bps.value() == 5000);
    assert(cubic.value().retrans_count == 7 && cubic.value().lost_count == 2);
    assert(cubic.value().pacing_rate_bps == 111 && cubic.value().max_pacing_rate_bps == 222);
    assert(cubic.value().dctcp.ecn_mark_ppm == 250000);
    assert(!cubic.value().has_bbr && !cubic.value().has_dctcp);

    auto short_info = layout::decode_tcp_sample(
        info.view().window(0, layout::tcp_info_bytes - 1).expect("the window is inside the struct"), no_bytes,
        cntp::CcAlgorithm::Cubic);
    assert(short_info.err().expect("a short struct gives no sample") == topology::TelemetryError::GetTcpInfoFailed);

    put_field(info, layout::tcp_info_field::ca_state, layout::TcpCaState{9});
    topology::TcpInfoSnapshot const unknown =
        layout::decode_tcp_sample(info.view(), no_bytes, cntp::CcAlgorithm::Cubic).expect("the struct is full");
    assert(unknown.value().mode == topology::CongestionMode::Unknown);

    CongestionInfoBytes bbr{};
    put_field(bbr, layout::bbr_info_field::bw_lo, std::uint32_t{0x10});
    put_field(bbr, layout::bbr_info_field::bw_hi, std::uint32_t{0x1});
    put_field(bbr, layout::bbr_info_field::min_rtt, std::uint32_t{40});
    put_field(bbr, layout::bbr_info_field::pacing_gain, std::uint32_t{256});
    put_field(bbr, layout::bbr_info_field::cwnd_gain, std::uint32_t{512});
    topology::TcpInfoSnapshot const with_bbr =
        layout::decode_tcp_sample(info.view(), bbr.view(), cntp::CcAlgorithm::Bbr3).expect("the struct is full");
    assert(with_bbr.value().has_bbr);
    assert(with_bbr.value().btl_bw_bps.value() == 0x100000010ull);
    assert(with_bbr.value().rt_prop_us.value() == 40);
    assert(with_bbr.value().bbr.cwnd_gain_q8 == 512);
    assert(with_bbr.value().mode == topology::CongestionMode::BbrProbeBw);

    topology::TcpInfoSnapshot const short_bbr =
        layout::decode_tcp_sample(info.view(),
                                  bbr.view().window(0, layout::bbr_info_bytes - 1).expect("the window is inside"),
                                  cntp::CcAlgorithm::Bbr3)
            .expect("the struct is full");
    assert(!short_bbr.value().has_bbr);
    assert(short_bbr.value().mode == topology::CongestionMode::Unknown);

    CongestionInfoBytes dctcp{};
    put_field(dctcp, layout::dctcp_info_field::enabled, std::uint16_t{1});
    put_field(dctcp, layout::dctcp_info_field::alpha, std::uint32_t{512});
    put_field(dctcp, layout::dctcp_info_field::ab_ecn, std::uint32_t{25});
    put_field(dctcp, layout::dctcp_info_field::ab_tot, std::uint32_t{100});
    topology::TcpInfoSnapshot const with_dctcp =
        layout::decode_tcp_sample(info.view(), dctcp.view(), cntp::CcAlgorithm::Dctcp).expect("the struct is full");
    assert(with_dctcp.value().has_dctcp && with_dctcp.value().dctcp.enabled);
    assert(with_dctcp.value().dctcp.alpha_q10 == 512);
    assert(with_dctcp.value().dctcp.ecn_mark_ppm == 250000);
    crucible::test::pass("  test_decode_tcp_sample: PASSED\n");
}

// A TCP socket of the kernel that is not connected has the initial window
// of ten segments of 536 bytes.  The harvest decodes it, and the largest
// pacing rate that the door set, from the TCP_INFO of the kernel.
void test_live_tcp_info() {
    // A harvest reads socket options, and the test runner context carries
    // the IO and Block that the socket option gate asks for.
    eff::TestRunnerCtx test_ctx{::foundation::effects::testing::test()};
    ::fixy::fs::OwnedFd const tcp =
        descriptor_of(::fixy::net::mint_socket<::fixy::net::socket_kind::TcpStream>(test_ctx));
    assert(tcp.is_open());
    cntp::SocketFd const fd = ::fixy::mint_refined<::fixy::non_negative>(tcp.get());
    // Reno is part of each kernel, so the harvest reads no fields of BBR or
    // of DCTCP.
    assert(::fixy::net::set_tcp_congestion(test_ctx, fd, "reno").is_ok());
    assert(::fixy::net::set_max_pacing_rate(test_ctx, fd, 1000000).is_ok());

    topology::TcpInfoSnapshot const snapshot =
        topology::harvest_socket(test_ctx, fd).expect("the kernel gives the TCP_INFO of a TCP socket");
    assert(snapshot.value().algorithm == cntp::CcAlgorithm::Reno);
    assert(snapshot.value().mode == topology::CongestionMode::Open);
    assert(snapshot.value().cwnd_bytes.value() == 10u * 536u);
    assert(snapshot.value().max_pacing_rate_bps == 1000000);
    assert(!snapshot.value().has_bbr && !snapshot.value().has_dctcp);

    ::foundation::core::FixedArray<cntp::SocketFd, 1> const fds{fd};
    auto const aggregate =
        topology::harvest_per_link(test_ctx, nic(77), fds.view()).expect("the harvest of a nic link succeeded");
    assert(aggregate.sample_count == 1);

    auto wrong_cog = topology::harvest_per_link(test_ctx, gpu(88), fds.view());
    assert(wrong_cog.is_err());
    assert(wrong_cog.err().expect("a gpu is no nic") == topology::TelemetryError::InvalidNicCog);
    crucible::test::pass("  test_live_tcp_info: PASSED\n");
}

void test_aggregate_finalize_guard() {
    // Both aggregation entry points share one finalize helper, so an
    // empty span must leave every mean at zero rather than divide by a
    // sample count of zero.  One helper for both call sites is what
    // makes the two paths impossible to drift apart.
    auto empty = topology::aggregate_congestion(nic(101), {});
    assert(empty.sample_count == 0);
    assert(empty.mean_btl_bw_bps == 0);
    assert(empty.p50_rtt_us == 0);
    assert(empty.p95_rtt_us == 0);
    assert(empty.p95_btl_bw_bps == 0);
    assert(empty.nic_uuid == nic(101).uuid);

    ::foundation::core::FixedArray<topology::TcpInfoSnapshot, 3> const samples{
        sample(100000000ull, 1500ull, topology::CongestionMode::BbrProbeBw),
        sample(200000000ull, 2500ull, topology::CongestionMode::BbrProbeBw),
        sample(300000000ull, 3500ull, topology::CongestionMode::BbrProbeBw),
    };
    auto agg = topology::aggregate_congestion(nic(102), samples.view());
    assert(agg.sample_count == 3);
    assert(agg.mean_btl_bw_bps == 200000000ull);
    assert(agg.nic_uuid == nic(102).uuid);

    crucible::test::pass("  test_aggregate_finalize_guard: PASSED\n");
}

}  // namespace

int main() {
    static_assert(sizeof(topology::TcpInfoSnapshot) == sizeof(topology::CongestionSample));
    static_assert(topology::CtxFitsCongestionTelemetryStart<eff::ColdInitCtx>);
    static_assert(!topology::CtxFitsCongestionTelemetryStart<eff::BgDrainCtx>);
    static_assert(topology::CtxFitsCongestionTelemetryHarvest<eff::BgDrainCtx>);
    static_assert(!topology::CtxFitsCongestionTelemetryHarvest<eff::HotFgCtx>);
    static_assert(topology::CtxFitsCongestionTelemetryPoll<eff::BgLoadCtx>);
    static_assert(!topology::CtxFitsCongestionTelemetryPoll<eff::BgDrainCtx>,
                  "a poll reads sockets through getsockopt, and a drain context carries no IO");
    static_assert(std::same_as<topology::TcpInfoSnapshot::tag_type, ::fixy::tags::source::TcpInfo>);
    static_assert(std::is_trivially_copy_constructible_v<topology::CongestionSample>
                  && std::is_trivially_destructible_v<topology::CongestionSample>);
    static_assert(!std::is_trivially_copyable_v<topology::CongestionSample>,
                  "a refined field keeps bytes from becoming a sample");
    static_assert(!std::is_default_constructible_v<topology::CongestionTelemetryWorker<2, 8>>);

    ::fixy::report(::fixy::Sink::Out, "test_congestion_telemetry:\n");
    test_names_and_admission();
    test_aggregate_and_drift();
    test_aggregate_finalize_guard();
    test_worker_recording();
    test_decode_tcp_sample();
    test_live_tcp_info();
    crucible::test::pass("test_congestion_telemetry: all PASSED\n");
    return 0;
}
