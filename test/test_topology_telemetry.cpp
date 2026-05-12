#include <crucible/topology/Telemetry.h>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <string_view>
#include <type_traits>

namespace topology = crucible::topology;
namespace effects = crucible::effects;
namespace cog = crucible::cog;
namespace cntp = crucible::cntp;

using InitCtx = effects::ExecCtx<
    effects::Init,
    effects::ctx_numa::Any,
    effects::ctx_alloc::Unbound,
    effects::ctx_heat::Cold,
    effects::ctx_resid::DRAM,
    effects::Row<effects::Effect::Init>,
    effects::ctx_workload::Unspecified>;

using BgCtx = effects::ExecCtx<
    effects::Bg,
    effects::ctx_numa::Any,
    effects::ctx_alloc::Arena,
    effects::ctx_heat::Warm,
    effects::ctx_resid::L3,
    effects::Row<effects::Effect::Bg>,
    effects::ctx_workload::Unspecified>;

static cog::CogIdentity nic() {
    cog::CogIdentity n{};
    n.uuid = cog::Uuid{0x112, 0x1};
    n.kind = cog::CogKind::NicPort;
    return n;
}

static topology::TcpInfoSnapshot tcp_sample(std::uint64_t bps,
                                            std::uint64_t rtt_us,
                                            std::uint32_t in_flight) {
    topology::CongestionState state{
        .algorithm = cntp::CcAlgorithm::Bbr3,
        .btl_bw_bps = topology::PositiveBandwidthBps{
            bps, typename topology::PositiveBandwidthBps::Trusted{}},
        .rt_prop_us = topology::PositiveMicroseconds{
            rtt_us, typename topology::PositiveMicroseconds::Trusted{}},
        .cwnd_bytes = topology::PositiveWindowBytes{
            std::uint32_t{1}, typename topology::PositiveWindowBytes::Trusted{}},
        .ssthresh_bytes = topology::PositiveWindowBytes{
            std::uint32_t{1}, typename topology::PositiveWindowBytes::Trusted{}},
        .in_flight_bytes = in_flight,
        .mode = topology::CongestionMode::BbrProbeBw,
        .has_bbr = true,
    };
    return topology::TcpInfoSnapshot{state};
}

static void test_name_coverage() {
    assert(topology::nic_telemetry_error_name(
        topology::NicTelemetryError::InvalidNicCog) ==
        std::string_view{"InvalidNicCog"});
    std::printf("  test_name_coverage:                   PASSED\n");
}

static void test_parsers_and_drop_rate() {
    constexpr std::string_view netdev =
        "rx_bytes: 1000\n"
        "tx_bytes: 2000\n"
        "rx_packets: 100\n"
        "tx_packets: 100\n"
        "rx_dropped: 1\n"
        "tx_dropped: 1\n"
        "rx_fifo_errors: 1\n";
    auto counters = topology::parse_netdev_counters(
        topology::tag_external_telemetry_text(netdev));
    assert(counters.has_value());
    assert(counters->value().rx_bytes == 1000);
    assert(topology::netdev_drop_ppm(counters->value()) == 15'000);

    constexpr std::string_view qdisc =
        "backlog: 4096b 7p\n"
        "drops: 3\n"
        "overlimits: 4\n";
    auto backlog = topology::parse_qdisc_backlog(
        topology::tag_external_telemetry_text(qdisc));
    assert(backlog.has_value());
    assert(backlog->value().backlog_bytes == 4096);
    assert(backlog->value().backlog_packets == 7);

    constexpr std::string_view sysctl =
        "net.core.rmem_max = 67108864\n"
        "net.core.wmem_max = 67108864\n"
        "net.core.busy_poll = 50\n"
        "net.ipv4.tcp_rmem_max = 33554432\n"
        "net.ipv4.tcp_wmem_max = 33554432\n";
    auto sys = topology::parse_sysctl_snapshot(
        topology::tag_external_telemetry_text(sysctl));
    assert(sys.has_value());
    assert(sys->value().rmem_max_bytes == 67'108'864);
    assert(sys->value().busy_poll_us == 50);
    std::printf("  test_parsers_and_drop_rate:           PASSED\n");
}

static void test_effective_bandwidth_and_history() {
    InitCtx init{};
    BgCtx bg{};
    auto history = topology::mint_nic_telemetry_history<4>(init);
    auto counters = topology::declare_netdev_counters(topology::NetdevCounters{
        .rx_packets = 1000,
        .tx_packets = 1000,
    });
    auto backlog = topology::declare_qdisc_backlog(topology::QdiscBacklog{});
    auto sysctl = topology::declare_sysctl_snapshot(topology::SysctlSnapshot{
        .rmem_max_bytes = 67'108'864,
        .wmem_max_bytes = 67'108'864,
    });
    auto thermal = topology::declare_nic_thermal_sample(
        topology::NicThermalSample{.temperature_millicelsius = 41'000});

    auto first = topology::mint_nic_telemetry_snapshot(
        nic(), 100'000'000'000ull, counters, backlog, sysctl,
        tcp_sample(80'000'000'000ull, 1000, 1000), thermal, 1);
    assert(first.has_value());
    auto second = topology::mint_nic_telemetry_snapshot(
        nic(), 100'000'000'000ull, counters, backlog, sysctl,
        tcp_sample(50'000'000'000ull, 1000, 1000), thermal, 2);
    assert(second.has_value());
    assert(first->effective_bandwidth_bps.value() > second->effective_bandwidth_bps.value());

    assert(history.record(bg, *first) == topology::NicTelemetryError::None);
    assert(history.record(bg, *second) == topology::NicTelemetryError::None);
    auto current = history.current_snapshot(init);
    assert(current.has_value());
    assert(current->sequence == 2);

    auto drift = history.detect_drift(topology::NicTelemetryPolicy{
        .fairness_penalty_ppm = 100'000,
        .drift_drop_ppm = 100'000,
        .min_drift_samples = 2,
    });
    assert(drift.has_value());
    assert(drift->degraded);
    assert(drift->bandwidth_drop_ppm > 300'000);
    std::printf("  test_effective_bandwidth_and_history: PASSED\n");
}

static void test_static_gates() {
    static_assert(topology::CtxFitsNicTelemetryMint<InitCtx>);
    static_assert(!topology::CtxFitsNicTelemetryMint<BgCtx>);
    static_assert(topology::CtxFitsNicTelemetryRecord<BgCtx>);
    static_assert(!topology::CtxFitsNicTelemetryRecord<InitCtx>);
    static_assert(topology::CtxFitsNicTelemetryRead<InitCtx>);
    static_assert(sizeof(topology::ExternalTelemetryText) == sizeof(std::string_view));
    static_assert(sizeof(topology::DeclaredNetdevCounters) ==
                  sizeof(topology::NetdevCounters));
    static_assert(std::is_trivially_destructible_v<topology::NicTelemetrySnapshot>);
    std::printf("  test_static_gates:                     PASSED\n");
}

int main() {
    std::printf("test_topology_telemetry:\n");
    test_name_coverage();
    test_parsers_and_drop_rate();
    test_effective_bandwidth_and_history();
    test_static_gates();
    std::printf("test_topology_telemetry: all PASSED\n");
    return 0;
}
