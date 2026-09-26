#include <crucible/topology/Pingmesh.h>
#include <foundation/reflect/EnumName.h>

#include "test_assert.h"

#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <type_traits>

namespace cog = crucible::cog;
namespace eff = ::fixy;
namespace topology = crucible::topology;

static eff::ColdInitCtx init_ctx() { return eff::ColdInitCtx{::foundation::effects::testing::init()}; }

static eff::BgDrainCtx bg_ctx() { return eff::BgDrainCtx{::foundation::effects::testing::bg()}; }

static cog::CogIdentity peer(std::uint64_t lo) {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{0x134, lo};
    id.level = cog::CogLevel::L3_Chassis;
    id.kind = cog::CogKind::Server;
    return id;
}

static topology::DeclaredPingmeshMeasurement
measurement(cog::CogIdentity const& src, cog::CogIdentity const& dst, std::uint64_t latency_ns, std::uint64_t sequence,
            topology::PingmeshProbeStatus status = topology::PingmeshProbeStatus::Delivered) {
    return ::fixy::mint_tagged<::fixy::tags::source::Pingmesh>(topology::PingmeshMeasurement{
        .src = src.uuid,
        .dst = dst.uuid,
        .latency_ns = ::fixy::mint_refined<::fixy::positive>(latency_ns),
        .sequence = sequence,
        .status = status,
    });
}

// The log spelling of both enums comes from reflection.
static void test_names() {
    using ::foundation::reflect::enum_name;
    static_assert(enum_name(topology::PingmeshProbeStatus::Delivered) == "Delivered");
    static_assert(enum_name(topology::PingmeshError::Full) == "Full");
    volatile auto error = topology::PingmeshError::LatencyOutOfRange;
    assert(enum_name(static_cast<topology::PingmeshError>(error)) == std::string_view{"LatencyOutOfRange"});
    std::printf("  test_names:                         PASSED\n");
}

static void test_register_and_record_pairs() {
    auto mesh = topology::mint_pingmesh<eff::ColdInitCtx, 4>(init_ctx());
    std::array peers{peer(1), peer(2), peer(3)};
    assert(mesh.start_probing(init_ctx(), std::span{peers}) == topology::PingmeshError::None);
    assert(mesh.start_probing(init_ctx(), std::span{peers}) == topology::PingmeshError::DuplicatePeer);
    assert(mesh.enable_pair(init_ctx(), peers[0].uuid, peers[1].uuid) == topology::PingmeshError::None);
    assert(mesh.enable_pair(init_ctx(), peers[0].uuid, peers[0].uuid) == topology::PingmeshError::SelfPair);
    assert(mesh.enable_pair(init_ctx(), peers[0].uuid, peer(9).uuid) == topology::PingmeshError::UnknownPeer);

    assert(mesh.record_measurement(bg_ctx(), measurement(peers[0], peers[1], 1000, 1)) == topology::PingmeshError::None);
    assert(mesh.record_measurement(bg_ctx(), measurement(peers[0], peers[1], 2000, 2)) == topology::PingmeshError::None);
    assert(mesh.record_measurement(bg_ctx(), measurement(peers[0], peers[1], 3000, 3)) == topology::PingmeshError::None);

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
    auto mesh = topology::mint_pingmesh<eff::ColdInitCtx, 2>(init_ctx());
    std::array peers{peer(4), peer(5)};
    assert(mesh.start_probing(init_ctx(), std::span{peers}) == topology::PingmeshError::None);

    assert(mesh.record_measurement(bg_ctx(), measurement(peers[0], peers[1], 1, 10, topology::PingmeshProbeStatus::Lost))
           == topology::PingmeshError::None);
    assert(mesh.record_measurement(bg_ctx(),
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
    auto mesh = topology::mint_pingmesh<eff::ColdInitCtx, 2, 2, 1000>(init_ctx());
    std::array peers{peer(6), peer(7)};
    auto missing = peer(8);
    assert(mesh.start_probing(init_ctx(), std::span{peers}) == topology::PingmeshError::None);
    assert(mesh.record_measurement(bg_ctx(), measurement(peers[0], missing, 1, 1)) == topology::PingmeshError::UnknownPeer);
    assert(mesh.record_measurement(bg_ctx(), measurement(peers[0], peers[1], 2000, 2))
           == topology::PingmeshError::LatencyOutOfRange);
    auto const stats = mesh.pair_stats(peers[0].uuid, peers[1].uuid);
    assert(stats.sent == 1);
    assert(stats.delivered == 0);
    assert(stats.rejected == 1);
    std::printf("  test_unknown_and_out_of_range_rejected: PASSED\n");
}

// enable_pair admits a pair of two registered peers, and refuses a self
// pair and an unknown peer.
static void test_enable_pair() {
    auto mesh = topology::mint_pingmesh<eff::ColdInitCtx, 3>(init_ctx());
    std::array peers{peer(9), peer(10)};
    assert(mesh.start_probing(init_ctx(), std::span{peers}) == topology::PingmeshError::None);
    assert(mesh.enable_pair(init_ctx(), peers[0].uuid, peers[1].uuid) == topology::PingmeshError::None);
    assert(mesh.enable_pair(init_ctx(), peers[0].uuid, peers[0].uuid) == topology::PingmeshError::SelfPair);
    assert(mesh.enable_pair(init_ctx(), peers[0].uuid, peer(11).uuid) == topology::PingmeshError::UnknownPeer);
    std::printf("  test_enable_pair:                   PASSED\n");
}

// The pair counters are cache-line aligned, so an array of them must place
// every element on its own line. When they pack tighter than that, the
// per-pair grid of a small fleet spans fewer lines than it has pairs, and
// adjacent producer threads contend on every increment.
static void test_pair_counter_layout_invariants() {
    using PairCounters = topology::detail::AtomicPingmeshPairCounters;
    static_assert(alignof(PairCounters) >= 64, "AtomicPingmeshPairCounters must be cache-line-aligned");
    static_assert(sizeof(PairCounters) >= 64, "AtomicPingmeshPairCounters occupies a full cache line");

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
    static_assert(topology::CtxFitsPingmeshMint<eff::ColdInitCtx>);
    static_assert(!topology::CtxFitsPingmeshMint<eff::BgDrainCtx>);
    static_assert(topology::CtxFitsPingmeshRecord<eff::BgDrainCtx>);
    static_assert(!topology::CtxFitsPingmeshRecord<eff::HotFgCtx>);
    static_assert(!std::is_constructible_v<topology::Pingmesh<2>, topology::PingmeshConfig>);

    std::printf("test_topology_pingmesh: 6 groups\n");
    test_names();
    test_register_and_record_pairs();
    test_loss_and_rejection_accounting();
    test_unknown_and_out_of_range_rejected();
    test_enable_pair();
    test_pair_counter_layout_invariants();
    std::printf("test_topology_pingmesh: all passed\n");
    return 0;
}
