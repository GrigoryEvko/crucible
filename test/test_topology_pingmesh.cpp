#include <crucible/topology/Pingmesh.h>
#include <foundation/reflect/EnumName.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <type_traits>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

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
    CRUCIBLE_TEST_ASSERT(kTest, enum_name(static_cast<topology::PingmeshError>(error))
                                    == std::string_view{"LatencyOutOfRange"});
    ::fixy::check::pass(kTest, "  test_names:                         PASSED\n");
}

using ::fixy::index;

static void test_register_and_record_pairs() {
    auto mesh = topology::mint_pingmesh<eff::ColdInitCtx, 4>(init_ctx());
    ::foundation::core::FixedArray<cog::CogIdentity, 3> const peers{peer(1), peer(2), peer(3)};
    cog::CogIdentity const& first = peers.at(index<0>());
    cog::CogIdentity const& second = peers.at(index<1>());
    CRUCIBLE_TEST_ASSERT(kTest, mesh.start_probing(init_ctx(), peers.view()) == topology::PingmeshError::None);
    CRUCIBLE_TEST_ASSERT(kTest, mesh.start_probing(init_ctx(), peers.view()) == topology::PingmeshError::DuplicatePeer);
    CRUCIBLE_TEST_ASSERT(kTest, mesh.enable_pair(init_ctx(), first.uuid, second.uuid) == topology::PingmeshError::None);
    CRUCIBLE_TEST_ASSERT(kTest,
                         mesh.enable_pair(init_ctx(), first.uuid, first.uuid) == topology::PingmeshError::SelfPair);
    CRUCIBLE_TEST_ASSERT(kTest, mesh.enable_pair(init_ctx(), first.uuid, peer(9).uuid)
                                    == topology::PingmeshError::UnknownPeer);

    CRUCIBLE_TEST_ASSERT(kTest, mesh.record_measurement(bg_ctx(), measurement(first, second, 1000, 1))
                                    == topology::PingmeshError::None);
    CRUCIBLE_TEST_ASSERT(kTest, mesh.record_measurement(bg_ctx(), measurement(first, second, 2000, 2))
                                    == topology::PingmeshError::None);
    CRUCIBLE_TEST_ASSERT(kTest, mesh.record_measurement(bg_ctx(), measurement(first, second, 3000, 3))
                                    == topology::PingmeshError::None);

    auto const stats = mesh.pair_stats(first.uuid, second.uuid);
    CRUCIBLE_TEST_ASSERT(kTest, stats.sent == 3);
    CRUCIBLE_TEST_ASSERT(kTest, stats.delivered == 3);
    CRUCIBLE_TEST_ASSERT(kTest, stats.lost == 0);
    CRUCIBLE_TEST_ASSERT(kTest, stats.last_sequence == 3);
    CRUCIBLE_TEST_ASSERT(kTest, stats.p50_latency_ns != 0);
    CRUCIBLE_TEST_ASSERT(kTest, stats.p99_latency_ns != 0);
    CRUCIBLE_TEST_ASSERT(kTest, mesh.per_pair_latency(first.uuid, second.uuid).is_some());
    CRUCIBLE_TEST_ASSERT(kTest, mesh.per_pair_latency(second.uuid, second.uuid).is_none());
    ::fixy::check::pass(kTest, "  test_register_and_record_pairs:      PASSED\n");
}

static void test_loss_and_rejection_accounting() {
    auto mesh = topology::mint_pingmesh<eff::ColdInitCtx, 2>(init_ctx());
    ::foundation::core::FixedArray<cog::CogIdentity, 2> const peers{peer(4), peer(5)};
    cog::CogIdentity const& first = peers.at(index<0>());
    cog::CogIdentity const& second = peers.at(index<1>());
    CRUCIBLE_TEST_ASSERT(kTest, mesh.start_probing(init_ctx(), peers.view()) == topology::PingmeshError::None);

    CRUCIBLE_TEST_ASSERT(
        kTest, mesh.record_measurement(bg_ctx(), measurement(first, second, 1, 10, topology::PingmeshProbeStatus::Lost))
                   == topology::PingmeshError::None);
    CRUCIBLE_TEST_ASSERT(kTest, mesh.record_measurement(bg_ctx(), measurement(first, second, 1, 11,
                                                                              topology::PingmeshProbeStatus::Rejected))
                                    == topology::PingmeshError::None);

    auto const stats = mesh.pair_stats(first.uuid, second.uuid);
    CRUCIBLE_TEST_ASSERT(kTest, stats.sent == 2);
    CRUCIBLE_TEST_ASSERT(kTest, stats.delivered == 0);
    CRUCIBLE_TEST_ASSERT(kTest, stats.lost == 1);
    CRUCIBLE_TEST_ASSERT(kTest, stats.rejected == 1);

    auto const report = mesh.detect_anomalies();
    CRUCIBLE_TEST_ASSERT(kTest, report.entries.size() == 1);
    topology::PingmeshAnomaly const& anomaly = *report.entries.get(0).expect("the report holds one anomaly");
    CRUCIBLE_TEST_ASSERT(kTest, anomaly.anomalous);
    CRUCIBLE_TEST_ASSERT(kTest, anomaly.stats.lost == 1);
    ::fixy::check::pass(kTest, "  test_loss_and_rejection_accounting:  PASSED\n");
}

static void test_unknown_and_out_of_range_rejected() {
    auto mesh = topology::mint_pingmesh<eff::ColdInitCtx, 2, 2, 1000>(init_ctx());
    ::foundation::core::FixedArray<cog::CogIdentity, 2> const peers{peer(6), peer(7)};
    cog::CogIdentity const& first = peers.at(index<0>());
    cog::CogIdentity const& second = peers.at(index<1>());
    auto missing = peer(8);
    CRUCIBLE_TEST_ASSERT(kTest, mesh.start_probing(init_ctx(), peers.view()) == topology::PingmeshError::None);
    CRUCIBLE_TEST_ASSERT(kTest, mesh.record_measurement(bg_ctx(), measurement(first, missing, 1, 1))
                                    == topology::PingmeshError::UnknownPeer);
    CRUCIBLE_TEST_ASSERT(kTest, mesh.record_measurement(bg_ctx(), measurement(first, second, 2000, 2))
                                    == topology::PingmeshError::LatencyOutOfRange);
    auto const stats = mesh.pair_stats(first.uuid, second.uuid);
    CRUCIBLE_TEST_ASSERT(kTest, stats.sent == 1);
    CRUCIBLE_TEST_ASSERT(kTest, stats.delivered == 0);
    CRUCIBLE_TEST_ASSERT(kTest, stats.rejected == 1);
    ::fixy::check::pass(kTest, "  test_unknown_and_out_of_range_rejected: PASSED\n");
}

// enable_pair admits a pair of two registered peers, and refuses a self
// pair and an unknown peer.
static void test_enable_pair() {
    auto mesh = topology::mint_pingmesh<eff::ColdInitCtx, 3>(init_ctx());
    ::foundation::core::FixedArray<cog::CogIdentity, 2> const peers{peer(9), peer(10)};
    cog::CogIdentity const& first = peers.at(index<0>());
    cog::CogIdentity const& second = peers.at(index<1>());
    CRUCIBLE_TEST_ASSERT(kTest, mesh.start_probing(init_ctx(), peers.view()) == topology::PingmeshError::None);
    CRUCIBLE_TEST_ASSERT(kTest, mesh.enable_pair(init_ctx(), first.uuid, second.uuid) == topology::PingmeshError::None);
    CRUCIBLE_TEST_ASSERT(kTest,
                         mesh.enable_pair(init_ctx(), first.uuid, first.uuid) == topology::PingmeshError::SelfPair);
    CRUCIBLE_TEST_ASSERT(kTest, mesh.enable_pair(init_ctx(), first.uuid, peer(11).uuid)
                                    == topology::PingmeshError::UnknownPeer);
    ::fixy::check::pass(kTest, "  test_enable_pair:                   PASSED\n");
}

// The pair counters are cache-line aligned, so an array of them must place
// every element on its own line. When they pack tighter than that, the
// per-pair grid of a small fleet spans fewer lines than it has pairs, and
// adjacent producer threads contend on every increment.
static void test_pair_counter_layout_invariants() {
    using PairCounters = topology::detail::AtomicPingmeshPairCounters;
    static_assert(alignof(PairCounters) >= 64, "AtomicPingmeshPairCounters must be cache-line-aligned");
    static_assert(sizeof(PairCounters) >= 64, "AtomicPingmeshPairCounters occupies a full cache line");

    // Each cell starts a line, and two cells that start lines at different
    // addresses are at least one line apart.
    ::fixy::FixedArray<PairCounters, 16> grid{};
    constexpr std::size_t LINE = 64;
    for (::fixy::Index<16> const i : grid.indices()) {
        ::fixy::Address const addr = ::fixy::address_of(::fixy::to(grid.at(i)));
        CRUCIBLE_TEST_ASSERT(kTest, addr.is_aligned<LINE>());
        if (i.raw() > 0) {
            ::fixy::Address const prev_addr = ::fixy::address_of(
                ::fixy::to(grid.at(::fixy::Index<16>::check(i.raw() - 1).expect("the cell before a later cell"))));
            CRUCIBLE_TEST_ASSERT(kTest, !(addr == prev_addr));
        }
    }
    ::fixy::check::pass(kTest, "  test_pair_counter_layout_invariants: PASSED\n");
}

int main() {
    static_assert(topology::CtxFitsPingmeshMint<eff::ColdInitCtx>);
    static_assert(!topology::CtxFitsPingmeshMint<eff::BgDrainCtx>);
    static_assert(topology::CtxFitsPingmeshRecord<eff::BgDrainCtx>);
    static_assert(!topology::CtxFitsPingmeshRecord<eff::HotFgCtx>);
    static_assert(!std::is_constructible_v<topology::Pingmesh<2>, topology::PingmeshConfig>);

    ::fixy::report(::fixy::Sink::Out, "test_topology_pingmesh: 6 groups\n");
    test_names();
    test_register_and_record_pairs();
    test_loss_and_rejection_accounting();
    test_unknown_and_out_of_range_rejected();
    test_enable_pair();
    test_pair_counter_layout_invariants();
    ::fixy::check::pass(kTest, "test_topology_pingmesh: all passed\n");
    return 0;
}
