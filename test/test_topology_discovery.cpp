#include <crucible/topology/Discovery.h>
#include <crucible/topology/TextFields.h>
#include <foundation/reflect/EnumName.h>

#include "padding_bytes.h"
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include <cstdio>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace topology = crucible::topology;
namespace cog = crucible::cog;
namespace eff = ::fixy;

// The element at position of a view that the test filled.
template <class T>
[[nodiscard]] static T const& element_at(::fixy::View<T const> view, std::size_t position) {
    return *view.get(position).expect("the test reads an element that the view holds");
}

// The log spelling of the four enums comes from reflection.
static void test_enum_names_come_from_reflection() {
    using ::foundation::reflect::enum_name;
    static_assert(enum_name(topology::DiscoveryError::TooManyNodes) == "TooManyNodes");
    static_assert(enum_name(topology::DiscoverySource::EthtoolInfo) == "EthtoolInfo");
    static_assert(enum_name(topology::DiscoveryOutcome::Partial) == "Partial");
    static_assert(enum_name(topology::DiscoveryNodeKind::NicPort) == "NicPort");

    volatile auto source = topology::DiscoverySource::Lldp;
    CRUCIBLE_TEST_ASSERT(kTest, enum_name(static_cast<topology::DiscoverySource>(source)) == std::string_view{"Lldp"});
    ::fixy::check::pass(kTest, "  test_enum_names_come_from_reflection: PASSED\n");
}

static void test_lspci_and_graph_materialization() {
    eff::ColdInitCtx ctx{::foundation::effects::testing::init()};
    auto snapshot = topology::mint_discovery_snapshot(ctx);
    constexpr ::fixy::TextView lspci = "Slot:\t0000:00:00.0\n"
                                       "Class:\tPCI bridge\n"
                                       "Vendor:\tIntel Corporation\n"
                                       "Device:\tRoot Port\n"
                                       "\n"
                                       "Slot:\t0000:65:00.0\n"
                                       "Class:\tEthernet controller\n"
                                       "Vendor:\tMellanox Technologies\n"
                                       "Device:\tConnectX-6 Dx\n"
                                       "\n"
                                       "Slot:\t0000:17:00.0\n"
                                       "Class:\t3D controller\n"
                                       "Vendor:\tNVIDIA Corporation\n"
                                       "Device:\tH100 PCIe\n";

    auto const status = topology::parse_lspci_vmm_tree(topology::tag_external_discovery_text(lspci), snapshot)
                            .expect("the lspci tree parses");
    CRUCIBLE_TEST_ASSERT(kTest, status.records_seen == 3);
    CRUCIBLE_TEST_ASSERT(kTest, status.records_admitted == 3);
    CRUCIBLE_TEST_ASSERT(kTest, snapshot.node_count() == 3);
    CRUCIBLE_TEST_ASSERT(kTest, snapshot.edge_count() == 2);
    CRUCIBLE_TEST_ASSERT(kTest, element_at(snapshot.nodes(), 0).kind == cog::CogKind::PcieRoot);
    CRUCIBLE_TEST_ASSERT(kTest, element_at(snapshot.nodes(), 1).kind == cog::CogKind::NicPort);
    CRUCIBLE_TEST_ASSERT(kTest, element_at(snapshot.nodes(), 2).kind == cog::CogKind::Gpu);
    CRUCIBLE_TEST_ASSERT(kTest,
                         element_at(snapshot.nodes(), 1).vendor.value() == ::fixy::TextView{"Mellanox Technologies"});

    auto graph = snapshot.graph(ctx);
    CRUCIBLE_TEST_ASSERT(kTest, graph.node_count() == 3);
    CRUCIBLE_TEST_ASSERT(kTest, graph.edge_count() == 2);
    topology::TopologyEdge const edge = graph.edge_by_id(topology::EdgeId{0}).expect("the graph holds edge 0");
    CRUCIBLE_TEST_ASSERT(kTest, edge.kind == topology::LinkKind::PciE);
    CRUCIBLE_TEST_ASSERT(kTest, edge.peer == topology::NodeId{1});
    ::fixy::check::pass(kTest, "  test_lspci_and_graph_materialization: PASSED\n");
}

// The line rate of outside text checks the product of its count and its
// unit, so a count past 64 bits gives no rate in a constant evaluation too.
static_assert(topology::text::parse_line_rate(::fixy::TextView{"100G"}) == 12500000000ull);
static_assert(topology::text::parse_line_rate(::fixy::TextView{"25000M"}) == 3125000000ull);
static_assert(topology::text::parse_line_rate(::fixy::TextView{"100000000000G"}) == 0);
static_assert(topology::text::parse_line_rate(::fixy::TextView{"99999999999999M"}) == 0);
static_assert(topology::text::parse_line_rate(::fixy::TextView{"18446744073G"}) == 2305843009125000000ull);
static_assert(topology::text::parse_line_rate(::fixy::TextView{"999999999999999999999G"}) == 0);
static_assert(topology::text::parse_line_rate(::fixy::TextView{"G"}) == 0);

static void test_ethtool_features_and_lldp() {
    eff::ColdInitCtx ctx{::foundation::effects::testing::init()};
    auto snapshot = topology::mint_discovery_snapshot(ctx);
    topology::DiscoveryNodeFact local{
        .kind = cog::CogKind::NicPort,
        .vendor = topology::tag_vendor_discovery_string("Mellanox"),
        .model = topology::tag_vendor_discovery_string("ConnectX-6"),
        .bus_info = topology::tag_vendor_discovery_string("0000:65:00.0"),
    };
    std::uint16_t const local_idx = snapshot.add_node(local).expect("an empty snapshot has room for a node");

    constexpr ::fixy::TextView info = "driver: mlx5_core\n"
                                      "firmware-version: 22.39.1002\n"
                                      "bus-info: 0000:65:00.0\n";
    auto info_status = topology::parse_ethtool_info(topology::tag_external_discovery_text(info), snapshot, local_idx);
    CRUCIBLE_TEST_ASSERT(kTest, info_status.is_ok());
    CRUCIBLE_TEST_ASSERT(kTest,
                         element_at(snapshot.node_facts(), local_idx).driver.value() == ::fixy::TextView{"mlx5_core"});
    CRUCIBLE_TEST_ASSERT(kTest, element_at(snapshot.node_facts(), local_idx).firmware.value()
                                    == ::fixy::TextView{"22.39.1002"});
    CRUCIBLE_TEST_ASSERT(kTest, element_at(snapshot.nodes(), local_idx).firmware_revision.value()
                                    == topology::stable_discovery_hash(::fixy::TextView{"22.39.1002"}));

    constexpr ::fixy::TextView features = "tcp-segmentation-offload: on\n"
                                          "generic-segmentation-offload: on\n"
                                          "generic-receive-offload: on\n"
                                          "large-receive-offload: off\n"
                                          "tls-hw-tx-offload: on\n"
                                          "hw-tc-offload: on\n";
    auto const bits = topology::parse_ethtool_features(topology::tag_external_discovery_text(features))
                          .expect("the feature list parses");
    CRUCIBLE_TEST_ASSERT(kTest, bits.test(cog::NicFeature::Tso));
    CRUCIBLE_TEST_ASSERT(kTest, bits.test(cog::NicFeature::Gso));
    CRUCIBLE_TEST_ASSERT(kTest, bits.test(cog::NicFeature::Gro));
    CRUCIBLE_TEST_ASSERT(kTest, !bits.test(cog::NicFeature::Lro));
    CRUCIBLE_TEST_ASSERT(kTest, bits.test(cog::NicFeature::TcEbpf));
    // The input also sets tls-hw-tx-offload to on.  No NicFeature names that
    // key, so the parser sets only the four bits above.
    CRUCIBLE_TEST_ASSERT(kTest, bits.popcount() == 4);

    constexpr ::fixy::TextView lldp = "Interface: eth0\n"
                                      "LineRate: 100G\n"
                                      "SysName: tor-a\n"
                                      "PortID: swp17\n";
    auto const status = topology::parse_lldp_neighbors(topology::tag_external_discovery_text(lldp), snapshot)
                            .expect("the neighbour list parses");
    CRUCIBLE_TEST_ASSERT(kTest, status.records_admitted == 1);
    CRUCIBLE_TEST_ASSERT(kTest, snapshot.node_count() == 2);
    CRUCIBLE_TEST_ASSERT(kTest, snapshot.edge_count() == 1);
    auto graph = snapshot.graph(ctx);
    CRUCIBLE_TEST_ASSERT(kTest, element_at(graph.edges(), 0).kind == topology::LinkKind::Ethernet);
    CRUCIBLE_TEST_ASSERT(kTest, element_at(graph.edges(), 0).bandwidth_bytes_per_sec.value() == 12500000000ull);
    ::fixy::check::pass(kTest, "  test_ethtool_features_and_lldp:       PASSED\n");
}

static void test_lldp_record_state_reset() {
    eff::ColdInitCtx ctx{::foundation::effects::testing::init()};
    auto snapshot = topology::mint_discovery_snapshot(ctx);
    auto local_idx = snapshot.add_node(topology::DiscoveryNodeFact{
        .kind = cog::CogKind::NicPort,
        .vendor = topology::tag_vendor_discovery_string("Mellanox"),
        .model = topology::tag_vendor_discovery_string("ConnectX-6"),
        .bus_info = topology::tag_vendor_discovery_string("0000:65:00.0"),
    });
    CRUCIBLE_TEST_ASSERT(kTest, local_idx.is_ok());

    constexpr ::fixy::TextView lldp = "Interface: eth0\n"
                                      "LineRate: 100G\n"
                                      "SysName: tor-a\n"
                                      "PortID: swp17\n"
                                      "\n"
                                      "Interface: eth0\n"
                                      "SysName: tor-b\n"
                                      "PortID: swp18\n";
    auto const status = topology::parse_lldp_neighbors(topology::tag_external_discovery_text(lldp), snapshot)
                            .expect("the neighbour list parses");
    CRUCIBLE_TEST_ASSERT(kTest, status.records_admitted == 2);
    CRUCIBLE_TEST_ASSERT(kTest, snapshot.edge_count() == 2);

    auto graph = snapshot.graph(ctx);
    CRUCIBLE_TEST_ASSERT(kTest, element_at(graph.edges(), 0).bandwidth_bytes_per_sec.value() == 12500000000ull);
    CRUCIBLE_TEST_ASSERT(kTest, element_at(graph.edges(), 1).bandwidth_bytes_per_sec.value() == 0);
    ::fixy::check::pass(kTest, "  test_lldp_record_state_reset:         PASSED\n");
}

// A line rate whose count of bits per second does not fit 64 bits gives no
// rate.  The old parse multiplied with no check, so 100000000000G wrapped to a
// small rate that the snapshot stored as a bandwidth.  The largest rate that
// fits still gives its bytes per second.
static void test_lldp_line_rate_past_64_bits() {
    eff::ColdInitCtx ctx{::foundation::effects::testing::init()};
    auto snapshot = topology::mint_discovery_snapshot(ctx);
    auto local_idx = snapshot.add_node(topology::DiscoveryNodeFact{
        .kind = cog::CogKind::NicPort,
        .vendor = topology::tag_vendor_discovery_string("Mellanox"),
        .model = topology::tag_vendor_discovery_string("ConnectX-6"),
        .bus_info = topology::tag_vendor_discovery_string("0000:65:00.0"),
    });
    CRUCIBLE_TEST_ASSERT(kTest, local_idx.is_ok());

    constexpr ::fixy::TextView lldp = "Interface: eth0\n"
                                      "LineRate: 100000000000G\n"
                                      "SysName: tor-a\n"
                                      "PortID: swp17\n"
                                      "\n"
                                      "Interface: eth0\n"
                                      "LineRate: 99999999999999M\n"
                                      "SysName: tor-b\n"
                                      "PortID: swp18\n"
                                      "\n"
                                      "Interface: eth0\n"
                                      "LineRate: 18446744073G\n"
                                      "SysName: tor-c\n"
                                      "PortID: swp19\n";
    auto const status = topology::parse_lldp_neighbors(topology::tag_external_discovery_text(lldp), snapshot)
                            .expect("the neighbour list parses");
    CRUCIBLE_TEST_ASSERT(kTest, status.records_admitted == 3);
    auto graph = snapshot.graph(ctx);
    CRUCIBLE_TEST_ASSERT(kTest, element_at(graph.edges(), 0).bandwidth_bytes_per_sec.value() == 0
                                    && "a rate of gigabits past 64 bits gives no rate");
    CRUCIBLE_TEST_ASSERT(kTest, element_at(graph.edges(), 1).bandwidth_bytes_per_sec.value() == 0
                                    && "a rate of megabits past 64 bits gives no rate");
    CRUCIBLE_TEST_ASSERT(kTest, element_at(graph.edges(), 2).bandwidth_bytes_per_sec.value() == 2305843009125000000ull
                                    && "the largest rate of gigabits that fits gives its bytes per second");
    ::fixy::check::pass(kTest, "  test_lldp_line_rate_past_64_bits:     PASSED\n");
}

static void test_graceful_empty_live_discovery() {
    eff::ColdInitCtx ctx{::foundation::effects::testing::init()};
    auto snapshot = topology::mint_discovery_snapshot<4, 4>(ctx);
    auto result = topology::discover_local_topology(ctx, snapshot);
    CRUCIBLE_TEST_ASSERT(kTest, result.node_count() == 0);
    CRUCIBLE_TEST_ASSERT(kTest, snapshot.report().view().size() == 1);
    CRUCIBLE_TEST_ASSERT(kTest,
                         element_at(snapshot.report().view(), 0).outcome == topology::DiscoveryOutcome::NotAttempted);

    eff::BgDrainCtx bg{::foundation::effects::testing::bg()};
    auto const trigger =
        topology::notify_rediscovery_trigger(bg, topology::DiscoverySource::Udev).expect("a trigger gives a status");
    CRUCIBLE_TEST_ASSERT(kTest, trigger.source == topology::DiscoverySource::Udev);
    ::fixy::check::pass(kTest, "  test_graceful_empty_live_discovery:   PASSED\n");
}

static void test_static_gates() {
    static_assert(topology::CtxFitsDiscoveryInit<eff::ColdInitCtx>);
    static_assert(!topology::CtxFitsDiscoveryInit<eff::BgDrainCtx>);
    static_assert(!topology::CtxFitsDiscoveryInit<eff::TestRunnerCtx>);
    static_assert(topology::CtxFitsDiscoveryBg<eff::BgDrainCtx>);
    static_assert(!topology::CtxFitsDiscoveryBg<int>);
    static_assert(!topology::DiscoveryShape<0, 1>);
    static_assert(topology::DiscoveryShape<1, 1>);
    static_assert(sizeof(topology::ExternalDiscoveryText) == sizeof(::fixy::TextView));
    static_assert(std::is_trivially_destructible_v<topology::DiscoverySnapshot<16, 32>>);
    static_assert(!std::is_default_constructible_v<topology::DiscoverySnapshot<16, 32>>);
    static_assert(std::is_same_v<decltype(topology::mint_discovery_snapshot(std::declval<eff::ColdInitCtx const&>())),
                                 topology::DefaultDiscoverySnapshot>);
    ::fixy::check::pass(kTest, "  test_static_gates:                    PASSED\n");
}

// A snapshot holds 64 identities, 64 node facts and 128 edge facts, and each
// padding byte of one of them costs one store for each element at each
// initialization of an automatic snapshot (padding_bytes.h).
static void test_snapshot_elements_have_no_padding_byte() {
    crucible::test::expect_no_padding_byte<^^cog::CogIdentity>(kTest);
    crucible::test::expect_no_padding_byte<^^topology::DiscoveryNodeFact>(kTest);
    crucible::test::expect_no_padding_byte<^^topology::DiscoveryEdgeFact>(kTest);
    ::fixy::check::pass(kTest, "  test_snapshot_elements_have_no_padding_byte: PASSED\n");
}

int main() {
    ::fixy::report(::fixy::Sink::Out, "test_topology_discovery:\n");
    test_enum_names_come_from_reflection();
    test_lspci_and_graph_materialization();
    test_ethtool_features_and_lldp();
    test_lldp_record_state_reset();
    test_lldp_line_rate_past_64_bits();
    test_graceful_empty_live_discovery();
    test_static_gates();
    test_snapshot_elements_have_no_padding_byte();
    ::fixy::check::pass(kTest, "test_topology_discovery: all PASSED\n");
    return 0;
}
