#include <crucible/topology/Telemetry.h>
#include <crucible/topology/TopologyGraph.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/reflect/EnumName.h>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <string_view>
#include <type_traits>

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

static void test_enumerator_names() {
    using ::foundation::reflect::enum_name;
    assert(enum_name(topology::NicTelemetryError::InvalidNicCog) == std::string_view{"InvalidNicCog"});
    assert(enum_name(static_cast<topology::NicTelemetryError>(0xFF)) == std::string_view{"<unknown NicTelemetryError>"});
    std::printf("  test_enumerator_names:                PASSED\n");
}

static void test_parsers_and_drop_rate() {
    constexpr std::string_view netdev = "rx_bytes: 1000\n"
                                        "tx_bytes: 2000\n"
                                        "rx_packets: 100\n"
                                        "tx_packets: 100\n"
                                        "rx_dropped: 1\n"
                                        "tx_dropped: 1\n"
                                        "rx_fifo_errors: 1\n";
    auto counters = topology::parse_netdev_counters(topology::tag_external_telemetry_text(netdev));
    assert(counters.has_value());
    assert(counters->value().rx_bytes == 1000);
    assert(topology::netdev_drop_ppm(counters->value()) == 15000);

    constexpr std::string_view qdisc = "backlog: 4096b 7p\n"
                                       "drops: 3\n"
                                       "overlimits: 4\n";
    auto backlog = topology::parse_qdisc_backlog(topology::tag_external_telemetry_text(qdisc));
    assert(backlog.has_value());
    assert(backlog->value().backlog_bytes == 4096);
    assert(backlog->value().backlog_packets == 7);

    constexpr std::string_view sysctl = "net.core.rmem_max = 67108864\n"
                                        "net.core.wmem_max = 67108864\n"
                                        "net.core.busy_poll = 50\n"
                                        "net.ipv4.tcp_rmem_max = 33554432\n"
                                        "net.ipv4.tcp_wmem_max = 33554432\n";
    auto sys = topology::parse_sysctl_snapshot(topology::tag_external_telemetry_text(sysctl));
    assert(sys.has_value());
    assert(sys->value().rmem_max_bytes == 67108864);
    assert(sys->value().busy_poll_us == 50);

    auto empty = topology::parse_netdev_counters(topology::tag_external_telemetry_text("  \n"));
    assert(!empty.has_value());
    assert(empty.error() == topology::NicTelemetryError::EmptyInput);
    auto unknown = topology::parse_sysctl_snapshot(topology::tag_external_telemetry_text("net.core.other = 1\n"));
    assert(!unknown.has_value());
    assert(unknown.error() == topology::NicTelemetryError::MissingRequiredField);
    std::printf("  test_parsers_and_drop_rate:           PASSED\n");
}

static void test_snapshot_mint() {
    Samples samples{};
    auto first = topology::mint_nic_telemetry_snapshot(nic(), 100000000000ull, samples.counters, samples.backlog,
                                                       samples.sysctl, tcp_sample(80000000000ull, 1000, 1000),
                                                       samples.thermal, 1);
    assert(first.has_value());
    assert(first->nic_uuid() == nic().uuid);
    assert(first->sequence() == 1);
    assert(first->line_rate_bps().value() == 100000000000ull);
    assert(first->tcp().peek().value().btl_bw_bps.value() == 80000000000ull);
    auto recomputed = topology::compute_effective_bandwidth(*first);
    assert(recomputed.has_value());
    assert(std::fabs(recomputed->value() - first->effective_bandwidth_bps().value()) < 1.0);

    auto gpu = nic();
    gpu.kind = cog::CogKind::Gpu;
    auto wrong_kind = topology::mint_nic_telemetry_snapshot(gpu, 1, samples.counters, samples.backlog, samples.sysctl,
                                                            tcp_sample(1, 1, 0), samples.thermal, 1);
    assert(!wrong_kind.has_value());
    assert(wrong_kind.error() == topology::NicTelemetryError::InvalidNicCog);

    auto zero = nic();
    zero.uuid = cog::Uuid{};
    auto zero_uuid = topology::mint_nic_telemetry_snapshot(zero, 1, samples.counters, samples.backlog, samples.sysctl,
                                                           tcp_sample(1, 1, 0), samples.thermal, 1);
    assert(!zero_uuid.has_value());
    assert(zero_uuid.error() == topology::NicTelemetryError::InvalidNicCog);
    std::printf("  test_snapshot_mint:                   PASSED\n");
}

static void test_effective_bandwidth_and_history() {
    auto init = init_ctx();
    auto bg = bg_ctx();
    auto history = topology::mint_nic_telemetry_history<4>(init);
    assert(history.count(init) == 0);
    auto empty = history.current_snapshot(bg);
    assert(!empty.has_value());
    assert(empty.error() == topology::NicTelemetryError::EmptyHistory);
    assert(!history.detect_drift(bg).has_value());

    Samples samples{};
    auto first = topology::mint_nic_telemetry_snapshot(nic(), 100000000000ull, samples.counters, samples.backlog,
                                                       samples.sysctl, tcp_sample(80000000000ull, 1000, 1000),
                                                       samples.thermal, 1);
    assert(first.has_value());
    auto second = topology::mint_nic_telemetry_snapshot(nic(), 100000000000ull, samples.counters, samples.backlog,
                                                        samples.sysctl, tcp_sample(50000000000ull, 1000, 1000),
                                                        samples.thermal, 2);
    assert(second.has_value());
    assert(first->effective_bandwidth_bps().value() > second->effective_bandwidth_bps().value());

    assert(history.record(bg, *first) == topology::NicTelemetryError::None);
    assert(history.record(bg, *second) == topology::NicTelemetryError::None);
    assert(history.count(bg) == 2);
    auto current = history.current_snapshot(init);
    assert(current.has_value());
    assert(current->sequence() == 2);

    auto drift = history.detect_drift(bg, topology::NicTelemetryPolicy{
                                              .fairness_penalty_ppm = 100000,
                                              .drift_drop_ppm = 100000,
                                              .min_drift_samples = 2,
                                          });
    assert(drift.has_value());
    assert(drift->degraded);
    assert(drift->bandwidth_drop_ppm > 300000);
    assert(drift->nic_uuid == nic().uuid);

    // The ring keeps the last four, so the oldest after five records is
    // the second one.
    for (std::uint64_t sequence = 3; sequence <= 5; ++sequence) {
        auto next = topology::mint_nic_telemetry_snapshot(nic(), 100000000000ull, samples.counters, samples.backlog,
                                                          samples.sysctl, tcp_sample(50000000000ull, 1000, 1000),
                                                          samples.thermal, sequence);
        assert(next.has_value());
        assert(history.record(bg, *next) == topology::NicTelemetryError::None);
    }
    assert(history.count(bg) == 4);
    assert(history.current_snapshot(bg)->sequence() == 5);
    auto steady = history.detect_drift(bg);
    assert(steady.has_value());
    assert(!steady->degraded);
    assert(steady->bandwidth_drop_ppm == 0);
    std::printf("  test_effective_bandwidth_and_history: PASSED\n");
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
    static_assert(sizeof(topology::ExternalTelemetryText) == sizeof(std::string_view));
    static_assert(sizeof(topology::DeclaredNetdevCounters) == sizeof(topology::NetdevCounters));
    static_assert(std::is_trivially_destructible_v<topology::NicTelemetrySnapshot>);
    // The per-socket sample and the health class of a link have two names,
    // so the telemetry headers and the graph header build in one TU.
    static_assert(std::is_enum_v<topology::CongestionState> && std::is_class_v<topology::CongestionSample>);
    std::printf("  test_static_gates:                     PASSED\n");
}

int main() {
    std::printf("test_topology_telemetry:\n");
    test_enumerator_names();
    test_parsers_and_drop_rate();
    test_snapshot_mint();
    test_effective_bandwidth_and_history();
    test_static_gates();
    std::printf("test_topology_telemetry: all PASSED\n");
    return 0;
}
