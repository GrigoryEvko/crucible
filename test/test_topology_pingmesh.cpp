#include <crucible/topology/Pingmesh.h>

#include "test_assert.h"

#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <string_view>

namespace cog = crucible::cog;
namespace effects = crucible::effects;
namespace safety = crucible::safety;
namespace topology = crucible::topology;

static cog::CogIdentity peer(std::uint64_t lo) {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{0x134, lo};
    id.level = cog::CogLevel::L3_Chassis;
    id.kind = cog::CogKind::Server;
    return id;
}

static topology::DeclaredPingmeshMeasurement
measurement(cog::CogIdentity const& src,
            cog::CogIdentity const& dst,
            std::uint64_t latency_ns,
            std::uint64_t sequence,
            topology::PingmeshProbeStatus status =
                topology::PingmeshProbeStatus::Delivered) {
    return topology::DeclaredPingmeshMeasurement{
        topology::PingmeshMeasurement{
            .src = src.uuid,
            .dst = dst.uuid,
            .latency_ns = topology::PositivePingmeshLatencyNs{latency_ns},
            .sequence = sequence,
            .status = status,
        }};
}

static void test_names() {
    assert(topology::pingmesh_probe_status_name(
        topology::PingmeshProbeStatus::Delivered) == std::string_view{"Delivered"});
    assert(topology::pingmesh_error_name(topology::PingmeshError::Full)
           == std::string_view{"Full"});
    std::printf("  test_names:                         PASSED\n");
}

static void test_register_and_record_pairs() {
    auto mesh = topology::mint_pingmesh<effects::ColdInitCtx, 4>(
        effects::ColdInitCtx{});
    std::array peers{peer(1), peer(2), peer(3)};
    assert(mesh.start_probing(effects::ColdInitCtx{}, std::span{peers})
           == topology::PingmeshError::None);
    assert(mesh.start_probing(effects::ColdInitCtx{}, std::span{peers})
           == topology::PingmeshError::DuplicatePeer);

    assert(mesh.record_measurement(effects::BgDrainCtx{},
        measurement(peers[0], peers[1], 1'000, 1)) == topology::PingmeshError::None);
    assert(mesh.record_measurement(effects::BgDrainCtx{},
        measurement(peers[0], peers[1], 2'000, 2)) == topology::PingmeshError::None);
    assert(mesh.record_measurement(effects::BgDrainCtx{},
        measurement(peers[0], peers[1], 3'000, 3)) == topology::PingmeshError::None);

    auto const stats = mesh.pair_stats(peers[0].uuid, peers[1].uuid);
    assert(stats.sent == 3);
    assert(stats.delivered == 3);
    assert(stats.lost == 0);
    assert(stats.last_sequence == 3);
    assert(stats.p50_latency_ns != 0);
    assert(stats.p99_latency_ns != 0);
    assert(mesh.per_pair_latency(peers[0].uuid, peers[1].uuid) != nullptr);
    assert(mesh.per_pair_latency(peers[1].uuid, peers[1].uuid) == nullptr);
    std::printf("  test_register_and_record_pairs:      PASSED\n");
}

static void test_loss_and_rejection_accounting() {
    auto mesh = topology::mint_pingmesh<effects::ColdInitCtx, 2>(
        effects::ColdInitCtx{});
    std::array peers{peer(4), peer(5)};
    assert(mesh.start_probing(effects::ColdInitCtx{}, std::span{peers})
           == topology::PingmeshError::None);

    assert(mesh.record_measurement(effects::BgDrainCtx{},
        measurement(peers[0], peers[1], 1, 10, topology::PingmeshProbeStatus::Lost))
        == topology::PingmeshError::None);
    assert(mesh.record_measurement(effects::BgDrainCtx{},
        measurement(peers[0], peers[1], 1, 11, topology::PingmeshProbeStatus::Rejected))
        == topology::PingmeshError::None);

    auto const stats = mesh.pair_stats(peers[0].uuid, peers[1].uuid);
    assert(stats.sent == 2);
    assert(stats.delivered == 0);
    assert(stats.lost == 1);
    assert(stats.rejected == 1);

    auto const report = mesh.detect_anomalies();
    assert(report.count == 1);
    assert(report.entries[0].anomalous);
    assert(report.entries[0].stats.lost == 1);
    std::printf("  test_loss_and_rejection_accounting:  PASSED\n");
}

static void test_unknown_and_out_of_range_rejected() {
    auto mesh = topology::mint_pingmesh<
        effects::ColdInitCtx, 2, 2, 1'000>(effects::ColdInitCtx{});
    std::array peers{peer(6), peer(7)};
    auto missing = peer(8);
    assert(mesh.start_probing(effects::ColdInitCtx{}, std::span{peers})
           == topology::PingmeshError::None);
    assert(mesh.record_measurement(effects::BgDrainCtx{},
        measurement(peers[0], missing, 1, 1)) == topology::PingmeshError::UnknownPeer);
    assert(mesh.record_measurement(effects::BgDrainCtx{},
        measurement(peers[0], peers[1], 2'000, 2))
        == topology::PingmeshError::LatencyOutOfRange);
    auto const stats = mesh.pair_stats(peers[0].uuid, peers[1].uuid);
    assert(stats.sent == 1);
    assert(stats.delivered == 0);
    assert(stats.rejected == 1);
    std::printf("  test_unknown_and_out_of_range_rejected: PASSED\n");
}

// fixy-A5-011 regression: AtomicPingmeshPairCounters has alignas(64), so an
// `std::array<AtomicPingmeshPairCounters, N>` (the per-pair grid embedded in
// Pingmesh::counters_) must place every element on a distinct cache line.
// Without the fix, a 4×4 fleet's 16-pair grid fit inside ~10 cache lines
// and adjacent producer threads contended on every fetch_add.
static void test_pair_counter_layout_invariants() {
    using PairCounters = topology::detail::AtomicPingmeshPairCounters;
    static_assert(alignof(PairCounters) >= 64,
                  "AtomicPingmeshPairCounters must be cache-line-aligned");
    static_assert(sizeof(PairCounters) >= 64,
                  "AtomicPingmeshPairCounters occupies a full cache line");

    std::array<PairCounters, 16> grid{};
    constexpr std::uintptr_t LINE = 64;
    for (std::size_t i = 0; i < grid.size(); ++i) {
        auto const addr = std::bit_cast<std::uintptr_t>(&grid[i]);
        assert((addr % LINE) == 0);
        if (i > 0) {
            auto const prev_addr = std::bit_cast<std::uintptr_t>(&grid[i - 1]);
            assert(addr - prev_addr >= LINE);
        }
    }
    std::printf("  test_pair_counter_layout_invariants: PASSED\n");
}

int main() {
    static_assert(topology::CtxFitsPingmeshMint<effects::ColdInitCtx>);
    static_assert(!topology::CtxFitsPingmeshMint<effects::BgDrainCtx>);
    static_assert(topology::CtxFitsPingmeshRecord<effects::BgDrainCtx>);
    static_assert(!topology::CtxFitsPingmeshRecord<effects::HotFgCtx>);

    std::printf("test_topology_pingmesh: 5 groups\n");
    test_names();
    test_register_and_record_pairs();
    test_loss_and_rejection_accounting();
    test_unknown_and_out_of_range_rejected();
    test_pair_counter_layout_invariants();
    std::printf("test_topology_pingmesh: all passed\n");
    return 0;
}
