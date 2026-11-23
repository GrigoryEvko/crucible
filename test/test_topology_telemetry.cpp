#include <crucible/topology/Telemetry.h>
#include <crucible/topology/TopologyGraph.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/reflect/EnumName.h>

#include <fixy/Core.h>
#include <fixy/check/Check.h>
#include <cmath>
#include <cstdio>
#include <string_view>
#include <type_traits>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace topology = crucible::topology;
namespace eff = ::fixy;
namespace cog = crucible::cog;
namespace cntp = crucible::cntp;

static eff::ColdInitCtx init_ctx() { return eff::ColdInitCtx{::foundation::effects::testing::init()}; }

static eff::BgDrainCtx bg_ctx() { return eff::BgDrainCtx{::foundation::effects::testing::bg()}; }

static cog::CogIdentity nic() {
    cog::CogIdentity n{};
    n.uuid = cog::Uuid{0x112, 0x1};
    n.kind = cog::CogKind::NicPort;
    return n;
}

static topology::TcpInfoSnapshot tcp_sample(std::uint64_t bps, std::uint64_t rtt_us, std::uint32_t in_flight) {
    topology::CongestionSample state{
        .algorithm = cntp::CcAlgorithm::Bbr3,
        .btl_bw_bps = ::fixy::mint_refined<::fixy::positive>(bps),
        .rt_prop_us = ::fixy::mint_refined<::fixy::positive>(rtt_us),
        .cwnd_bytes = ::fixy::mint_refined<::fixy::positive>(std::uint32_t{1}),
        .ssthresh_bytes = ::fixy::mint_refined<::fixy::positive>(std::uint32_t{1}),
        .in_flight_bytes = in_flight,
        .mode = topology::CongestionMode::BbrProbeBw,
        .has_bbr = true,
    };
    return ::fixy::mint_tagged<::fixy::tags::source::TcpInfo>(state);
}

struct Samples {
    topology::DeclaredNetdevCounters counters = topology::declare_netdev_counters(topology::NetdevCounters{
        .rx_packets = 1000,
        .tx_packets = 1000,
    });
    topology::DeclaredQdiscBacklog backlog = topology::declare_qdisc_backlog(topology::QdiscBacklog{});
    topology::DeclaredSysctlSnapshot sysctl = topology::declare_sysctl_snapshot(topology::SysctlSnapshot{
        .rmem_max_bytes = 67108864,
        .wmem_max_bytes = 67108864,
    });
    topology::DeclaredNicThermalSample thermal =
        topology::declare_nic_thermal_sample(topology::NicThermalSample{.temperature_millicelsius = 41000});
};

// The error of a result that the test knows to hold one.
template <class T>
[[nodiscard]] static topology::NicTelemetryError
error_of(::fixy::Result<T, topology::NicTelemetryError> const& result) {
    return result.err().expect("the result holds an error");
}

static void test_enumerator_names() {
    using ::fixy::enum_name_text;
    CRUCIBLE_TEST_ASSERT(kTest, enum_name_text(topology::NicTelemetryError::InvalidNicCog)
                                    == ::fixy::TextView{"InvalidNicCog"});
    CRUCIBLE_TEST_ASSERT(kTest, enum_name_text(static_cast<topology::NicTelemetryError>(0xFF))
                                    == ::fixy::TextView{"<unknown NicTelemetryError>"});
    ::fixy::check::pass(kTest, "  test_enumerator_names:                PASSED\n");
}

static void test_parsers_and_drop_rate() {
    constexpr ::fixy::TextView netdev = "rx_bytes: 1000\n"
                                        "tx_bytes: 2000\n"
                                        "rx_packets: 100\n"
                                        "tx_packets: 100\n"
                                        "rx_dropped: 1\n"
                                        "tx_dropped: 1\n"
                                        "rx_fifo_errors: 1\n";
    auto const counters =
        topology::parse_netdev_counters(topology::tag_external_telemetry_text(netdev)).expect("the netdev text parses");
    CRUCIBLE_TEST_ASSERT(kTest, counters.value().rx_bytes == 1000);
    CRUCIBLE_TEST_ASSERT(kTest, topology::netdev_drop_ppm(counters.value()) == 15000);

    constexpr ::fixy::TextView qdisc = "backlog: 4096b 7p\n"
                                       "drops: 3\n"
                                       "overlimits: 4\n";
    auto const backlog =
        topology::parse_qdisc_backlog(topology::tag_external_telemetry_text(qdisc)).expect("the qdisc text parses");
    CRUCIBLE_TEST_ASSERT(kTest, backlog.value().backlog_bytes == 4096);
    CRUCIBLE_TEST_ASSERT(kTest, backlog.value().backlog_packets == 7);

    constexpr ::fixy::TextView sysctl = "net.core.rmem_max = 67108864\n"
                                        "net.core.wmem_max = 67108864\n"
                                        "net.core.busy_poll = 50\n"
                                        "net.ipv4.tcp_rmem_max = 33554432\n"
                                        "net.ipv4.tcp_wmem_max = 33554432\n";
    auto const sys =
        topology::parse_sysctl_snapshot(topology::tag_external_telemetry_text(sysctl)).expect("the sysctl text parses");
    CRUCIBLE_TEST_ASSERT(kTest, sys.value().rmem_max_bytes == 67108864);
    CRUCIBLE_TEST_ASSERT(kTest, sys.value().busy_poll_us == 50);

    auto empty = topology::parse_netdev_counters(topology::tag_external_telemetry_text("  \n"));
    CRUCIBLE_TEST_ASSERT(kTest, empty.is_err());
    CRUCIBLE_TEST_ASSERT(kTest, error_of(empty) == topology::NicTelemetryError::EmptyInput);
    auto unknown = topology::parse_sysctl_snapshot(topology::tag_external_telemetry_text("net.core.other = 1\n"));
    CRUCIBLE_TEST_ASSERT(kTest, unknown.is_err());
    CRUCIBLE_TEST_ASSERT(kTest, error_of(unknown) == topology::NicTelemetryError::MissingRequiredField);
    ::fixy::check::pass(kTest, "  test_parsers_and_drop_rate:           PASSED\n");
}

// Each rate of the telemetry is the floor of an exact quotient of integers,
// so it has the same value on each target.  A long double has 64 bits of
// significand on x86_64 and 113 on aarch64.  The quotient of long double of
// the first pair below gave 129678 on x86_64 and 129677 on aarch64, and the
// floor is 129677.  A sum of counters past 64 bits saturates and does not
// wrap to a small count.
static void test_integer_rates() {
    constexpr std::uint64_t top = ::fixy::max_of<std::uint64_t>;
    CRUCIBLE_TEST_ASSERT(kTest, topology::netdev_drop_ppm(topology::NetdevCounters{
                                    .rx_packets = 18182783979729537871ull,
                                    .rx_dropped = 2357907060923367012ull,
                                }) == 129677);
    CRUCIBLE_TEST_ASSERT(kTest, topology::netdev_drop_ppm(topology::NetdevCounters{
                                    .rx_packets = top,
                                    .rx_dropped = top - 1,
                                }) == 999999);
    CRUCIBLE_TEST_ASSERT(kTest, topology::netdev_drop_ppm(topology::NetdevCounters{
                                    .rx_packets = 3,
                                    .rx_dropped = 1,
                                }) == 333333);
    CRUCIBLE_TEST_ASSERT(kTest, topology::netdev_drop_ppm(topology::NetdevCounters{
                                    .rx_packets = top,
                                    .tx_packets = 2,
                                    .rx_dropped = 1,
                                }) == 0);
    CRUCIBLE_TEST_ASSERT(kTest, topology::netdev_drop_ppm(topology::NetdevCounters{
                                    .rx_packets = 1,
                                    .rx_dropped = top,
                                    .tx_dropped = top,
                                }) == 1000000);

    topology::SysctlSnapshot const sysctl{.rmem_max_bytes = 67108864};
    CRUCIBLE_TEST_ASSERT(kTest, topology::sysctl_throughput_ceiling_bps(sysctl, 3) == 178956970666666);
    CRUCIBLE_TEST_ASSERT(
        kTest, topology::sysctl_throughput_ceiling_bps(topology::SysctlSnapshot{.rmem_max_bytes = top}, 1) == top);
    CRUCIBLE_TEST_ASSERT(kTest, topology::sysctl_throughput_ceiling_bps(topology::SysctlSnapshot{}, 1) == top);

    // A bottleneck of 80 Gbps less the fair share of 1000 bytes in flight
    // over 1000 us at 100000 ppm: 1000 * 8 * 100000 / 1000 = 800000 bps.
    Samples samples{};
    ::fixy::SequenceSource sampler = ::fixy::mint_sequence_source(init_ctx());
    ::fixy::SequenceStamp const at_one = sampler.advance();
    auto const snapshot =
        topology::mint_nic_telemetry_snapshot(nic(), 100000000000ull, samples.counters, samples.backlog, samples.sysctl,
                                              tcp_sample(80000000000ull, 1000, 1000), samples.thermal, sampler, at_one)
            .expect("a nic gives a snapshot");
    CRUCIBLE_TEST_ASSERT(
        kTest, ::fixy::narrow<std::uint64_t>(topology::compute_effective_bandwidth(snapshot).value()).value_or(0)
                   == 79999200000);
    // A penalty above the bottleneck leaves one bit per second.
    auto const starved =
        topology::mint_nic_telemetry_snapshot(nic(), 100000000000ull, samples.counters, samples.backlog, samples.sysctl,
                                              tcp_sample(1000, 1, 4000000000u), samples.thermal, sampler, at_one)
            .expect("a nic gives a snapshot");
    CRUCIBLE_TEST_ASSERT(
        kTest, ::fixy::narrow<std::uint64_t>(topology::compute_effective_bandwidth(starved).value()).value_or(0) == 1);
    ::fixy::check::pass(kTest, "  test_integer_rates:                   PASSED\n");
}

static void test_snapshot_mint() {
    Samples samples{};
    ::fixy::SequenceSource sampler = ::fixy::mint_sequence_source(init_ctx());
    ::fixy::SequenceStamp const at_one = sampler.advance();
    auto const first =
        topology::mint_nic_telemetry_snapshot(nic(), 100000000000ull, samples.counters, samples.backlog, samples.sysctl,
                                              tcp_sample(80000000000ull, 1000, 1000), samples.thermal, sampler, at_one)
            .expect("a nic gives a snapshot");
    CRUCIBLE_TEST_ASSERT(kTest, first.nic_uuid() == nic().uuid);
    CRUCIBLE_TEST_ASSERT(kTest, first.sequence() == 1);
    CRUCIBLE_TEST_ASSERT(kTest, first.line_rate_bps().value() == 100000000000ull);
    CRUCIBLE_TEST_ASSERT(kTest, first.tcp().peek().value().btl_bw_bps.value() == 80000000000ull);
    auto const recomputed = topology::compute_effective_bandwidth(first);
    CRUCIBLE_TEST_ASSERT(kTest, std::fabs(recomputed.value() - first.effective_bandwidth_bps().value()) < 1.0);

    auto gpu = nic();
    gpu.kind = cog::CogKind::Gpu;
    auto wrong_kind = topology::mint_nic_telemetry_snapshot(gpu, 1, samples.counters, samples.backlog, samples.sysctl,
                                                            tcp_sample(1, 1, 0), samples.thermal, sampler, at_one);
    CRUCIBLE_TEST_ASSERT(kTest, wrong_kind.is_err());
    CRUCIBLE_TEST_ASSERT(kTest, error_of(wrong_kind) == topology::NicTelemetryError::InvalidNicCog);

    auto zero = nic();
    zero.uuid = cog::Uuid{};
    auto zero_uuid = topology::mint_nic_telemetry_snapshot(zero, 1, samples.counters, samples.backlog, samples.sysctl,
                                                           tcp_sample(1, 1, 0), samples.thermal, sampler, at_one);
    CRUCIBLE_TEST_ASSERT(kTest, zero_uuid.is_err());
    CRUCIBLE_TEST_ASSERT(kTest, error_of(zero_uuid) == topology::NicTelemetryError::InvalidNicCog);
    ::fixy::check::pass(kTest, "  test_snapshot_mint:                   PASSED\n");
}

// The grade of each sample of a snapshot is the distance from its stamp to
// the current position of the sampler.  The sequence number of the snapshot
// is the position of the stamp, and it is no grade.
static void test_snapshot_grade_comes_from_the_sampler() {
    Samples samples{};
    ::fixy::SequenceSource sampler = ::fixy::mint_sequence_source(init_ctx());
    for (int step = 0; step < 5; ++step)
        static_cast<void>(sampler.advance());
    ::fixy::SequenceStamp const at_five = sampler.stamp();
    for (int step = 0; step < 2; ++step)
        static_cast<void>(sampler.advance());

    auto const current = topology::mint_nic_telemetry_snapshot(
                             nic(), 100000000000ull, samples.counters, samples.backlog, samples.sysctl,
                             tcp_sample(80000000000ull, 1000, 1000), samples.thermal, sampler, sampler.stamp())
                             .expect("a nic gives a snapshot");
    CRUCIBLE_TEST_ASSERT(kTest, current.sequence() == 7);
    CRUCIBLE_TEST_ASSERT(kTest,
                         current.netdev().is_fresh() && current.qdisc().is_fresh() && current.sysctl().is_fresh());
    CRUCIBLE_TEST_ASSERT(kTest, current.tcp().is_fresh() && current.thermal().is_fresh());

    auto const older =
        topology::mint_nic_telemetry_snapshot(nic(), 100000000000ull, samples.counters, samples.backlog, samples.sysctl,
                                              tcp_sample(80000000000ull, 1000, 1000), samples.thermal, sampler, at_five)
            .expect("a nic gives a snapshot");
    CRUCIBLE_TEST_ASSERT(kTest, older.sequence() == 5);
    CRUCIBLE_TEST_ASSERT(kTest, older.netdev().staleness().value == 2 && older.tcp().staleness().value == 2);
    CRUCIBLE_TEST_ASSERT(kTest, older.thermal().staleness().value == 2);
    ::fixy::check::pass(kTest, "  test_snapshot_grade_comes_from_the_sampler: PASSED\n");
}

static void test_effective_bandwidth_and_history() {
    auto init = init_ctx();
    auto bg = bg_ctx();
    auto history = topology::mint_nic_telemetry_history<4>(init);
    CRUCIBLE_TEST_ASSERT(kTest, history.count(init) == 0);
    auto empty = history.current_snapshot(bg);
    CRUCIBLE_TEST_ASSERT(kTest, empty.is_err());
    CRUCIBLE_TEST_ASSERT(kTest, error_of(empty) == topology::NicTelemetryError::EmptyHistory);
    CRUCIBLE_TEST_ASSERT(kTest, history.detect_drift(bg).is_err());

    Samples samples{};
    ::fixy::SequenceSource sampler = ::fixy::mint_sequence_source(init);
    ::fixy::SequenceStamp const at_one = sampler.advance();
    auto const first =
        topology::mint_nic_telemetry_snapshot(nic(), 100000000000ull, samples.counters, samples.backlog, samples.sysctl,
                                              tcp_sample(80000000000ull, 1000, 1000), samples.thermal, sampler, at_one)
            .expect("a nic gives a snapshot");
    ::fixy::SequenceStamp const at_two = sampler.advance();
    auto const second =
        topology::mint_nic_telemetry_snapshot(nic(), 100000000000ull, samples.counters, samples.backlog, samples.sysctl,
                                              tcp_sample(50000000000ull, 1000, 1000), samples.thermal, sampler, at_two)
            .expect("a nic gives a snapshot");
    CRUCIBLE_TEST_ASSERT(kTest, first.effective_bandwidth_bps().value() > second.effective_bandwidth_bps().value());

    CRUCIBLE_TEST_ASSERT(kTest, history.record(bg, first) == topology::NicTelemetryError::None);
    CRUCIBLE_TEST_ASSERT(kTest, history.record(bg, second) == topology::NicTelemetryError::None);
    CRUCIBLE_TEST_ASSERT(kTest, history.count(bg) == 2);
    auto const current = history.current_snapshot(init).expect("the history holds two snapshots");
    CRUCIBLE_TEST_ASSERT(kTest, current.sequence() == 2);

    auto const drift = history
                           .detect_drift(bg,
                                         topology::NicTelemetryPolicy{
                                             .fairness_penalty_ppm = 100000,
                                             .drift_drop_ppm = 100000,
                                             .min_drift_samples = 2,
                                         })
                           .expect("the history holds two snapshots");
    CRUCIBLE_TEST_ASSERT(kTest, drift.degraded);
    CRUCIBLE_TEST_ASSERT(kTest, drift.bandwidth_drop_ppm > 300000);
    CRUCIBLE_TEST_ASSERT(kTest, drift.nic_uuid == nic().uuid);

    // The ring keeps the last four, so the oldest after five records is
    // the second one.
    for (std::uint64_t sequence = 3; sequence <= 5; ++sequence) {
        ::fixy::SequenceStamp const sampled = sampler.advance();
        auto const next = topology::mint_nic_telemetry_snapshot(
                              nic(), 100000000000ull, samples.counters, samples.backlog, samples.sysctl,
                              tcp_sample(50000000000ull, 1000, 1000), samples.thermal, sampler, sampled)
                              .expect("a nic gives a snapshot");
        CRUCIBLE_TEST_ASSERT(kTest, next.sequence() == sequence);
        CRUCIBLE_TEST_ASSERT(kTest, history.record(bg, next) == topology::NicTelemetryError::None);
    }
    CRUCIBLE_TEST_ASSERT(kTest, history.count(bg) == 4);
    CRUCIBLE_TEST_ASSERT(kTest,
                         history.current_snapshot(bg).expect("the history holds four snapshots").sequence() == 5);
    auto const steady = history.detect_drift(bg).expect("the history holds four snapshots");
    CRUCIBLE_TEST_ASSERT(kTest, !steady.degraded);
    CRUCIBLE_TEST_ASSERT(kTest, steady.bandwidth_drop_ppm == 0);
    ::fixy::check::pass(kTest, "  test_effective_bandwidth_and_history: PASSED\n");
}

static void test_static_gates() {
    static_assert(topology::CtxFitsNicTelemetryMint<eff::ColdInitCtx>);
    static_assert(!topology::CtxFitsNicTelemetryMint<eff::BgDrainCtx>);
    static_assert(!topology::CtxFitsNicTelemetryMint<eff::HotFgCtx>);
    static_assert(topology::CtxFitsNicTelemetryRecord<eff::BgDrainCtx>);
    static_assert(!topology::CtxFitsNicTelemetryRecord<eff::ColdInitCtx>);
    static_assert(!topology::CtxFitsNicTelemetryRecord<eff::HotFgCtx>);
    static_assert(topology::CtxFitsNicTelemetryRead<eff::ColdInitCtx>);
    static_assert(topology::CtxFitsNicTelemetryRead<eff::BgDrainCtx>);
    static_assert(!topology::CtxFitsNicTelemetryRead<eff::HotFgCtx>);
    static_assert(!topology::CtxFitsNicTelemetryRead<eff::TestRunnerCtx>);
    static_assert(!topology::CtxFitsNicTelemetryRead<int>);
    static_assert(!std::is_default_constructible_v<topology::NicTelemetrySnapshot>);
    static_assert(!std::is_default_constructible_v<topology::NicTelemetryHistory<4>>);
    static_assert(sizeof(topology::ExternalTelemetryText) == sizeof(::fixy::TextView));
    static_assert(sizeof(topology::DeclaredNetdevCounters) == sizeof(topology::NetdevCounters));
    static_assert(std::is_trivially_destructible_v<topology::NicTelemetrySnapshot>);
    // The per-socket sample and the health class of a link have two names,
    // so the telemetry headers and the graph header build in one TU.
    static_assert(std::is_enum_v<topology::CongestionState> && std::is_class_v<topology::CongestionSample>);
    ::fixy::check::pass(kTest, "  test_static_gates:                     PASSED\n");
}

int main() {
    ::fixy::report(::fixy::Sink::Out, "test_topology_telemetry:\n");
    test_enumerator_names();
    test_parsers_and_drop_rate();
    test_integer_rates();
    test_snapshot_mint();
    test_snapshot_grade_comes_from_the_sampler();
    test_effective_bandwidth_and_history();
    test_static_gates();
    ::fixy::check::pass(kTest, "test_topology_telemetry: all PASSED\n");
    return 0;
}
