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

// The fact of the local port of the cases that read ethtool and LLDP text.
// The constructor of a vendor text from a literal checks its capacity at
// compile time.
[[nodiscard]] static topology::DiscoveryNodeFact local_port_fact() {
    return topology::DiscoveryNodeFact{
        .kind = cog::CogKind::NicPort,
        .vendor = topology::tag_vendor_discovery_string(topology::VendorDiscoveryText{"Mellanox"}),
        .model = topology::tag_vendor_discovery_string(topology::VendorDiscoveryText{"ConnectX-6"}),
        .bus_info = topology::tag_vendor_discovery_string(topology::VendorDiscoveryText{"0000:65:00.0"}),
    };
}

// The log spelling of the four enums comes from reflection.
static void test_enum_names_come_from_reflection() {
    using ::fixy::enum_name_text;
    static_assert(enum_name_text(topology::DiscoveryError::TooManyNodes) == "TooManyNodes");
    static_assert(enum_name_text(topology::DiscoverySource::EthtoolInfo) == "EthtoolInfo");
    static_assert(enum_name_text(topology::DiscoveryOutcome::Partial) == "Partial");
    static_assert(enum_name_text(topology::DiscoveryNodeKind::NicPort) == "NicPort");

    volatile auto source = topology::DiscoverySource::Lldp;
    CRUCIBLE_TEST_ASSERT(kTest,
                         enum_name_text(static_cast<topology::DiscoverySource>(source)) == ::fixy::TextView{"Lldp"});
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
    std::uint16_t const local_idx =
        snapshot.add_node(local_port_fact()).expect("an empty snapshot has room for a node");

    constexpr ::fixy::TextView info = "driver: mlx5_core\n"
                                      "firmware-version: 22.39.1002\n"
                                      "bus-info: 0000:65:00.0\n";
    auto info_status = topology::parse_ethtool_info(topology::tag_external_discovery_text(info), snapshot, local_idx);
    CRUCIBLE_TEST_ASSERT(kTest, info_status.is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, element_at(snapshot.node_facts(), local_idx).driver.value().view()
                                    == ::fixy::TextView{"mlx5_core"});
    CRUCIBLE_TEST_ASSERT(kTest, element_at(snapshot.node_facts(), local_idx).firmware.value().view()
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
    auto local_idx = snapshot.add_node(local_port_fact());
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
    auto local_idx = snapshot.add_node(local_port_fact());
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

// The characters of text in a heap text that the test owns, so a case can end
// the buffer that a parser read.
[[nodiscard]] static ::fixy::HeapText heap_text_of(::fixy::TextView text) {
    ::fixy::GrowText grown = ::fixy::mint_grow_text(text.size());
    grown.append(::foundation::effects::testing::test().alloc, text).expect("the text fits its maximum");
    return ::fixy::freeze(std::move(grown));
}

// Each vendor text of a snapshot stays readable after the end of the buffer
// that its parser read.  Each of the three parsers reads a heap text that
// ends before the reads, and each vendor text of a fact and of an identity
// comes from one of them.
static void test_vendor_texts_outlive_their_input() {
    eff::ColdInitCtx ctx{::foundation::effects::testing::init()};
    auto snapshot = topology::mint_discovery_snapshot(ctx);
    {
        ::fixy::HeapText const lspci = heap_text_of("Slot:\t0000:65:00.0\n"
                                                    "Class:\tEthernet controller\n"
                                                    "Vendor:\tMellanox Technologies\n"
                                                    "Device:\tConnectX-6 Dx\n");
        static_cast<void>(topology::parse_lspci_vmm_tree(topology::tag_external_discovery_text(lspci.view()), snapshot)
                              .expect("the lspci tree parses"));
    }
    {
        ::fixy::HeapText const info = heap_text_of("driver: mlx5_core\n"
                                                   "firmware-version: 22.39.1002\n"
                                                   "bus-info: 0000:65:00.1\n");
        static_cast<void>(topology::parse_ethtool_info(topology::tag_external_discovery_text(info.view()), snapshot, 0)
                              .expect("the driver info parses"));
    }
    {
        ::fixy::HeapText const lldp = heap_text_of("Interface: eth0\n"
                                                   "SysName: tor-a\n"
                                                   "PortID: ifname swp17\n");
        static_cast<void>(topology::parse_lldp_neighbors(topology::tag_external_discovery_text(lldp.view()), snapshot)
                              .expect("the neighbour list parses"));
    }
    topology::DiscoveryNodeFact const& local = element_at(snapshot.node_facts(), 0);
    topology::DiscoveryNodeFact const& peer = element_at(snapshot.node_facts(), 1);
    CRUCIBLE_TEST_ASSERT(kTest, local.vendor.value().view() == ::fixy::TextView{"Mellanox Technologies"});
    CRUCIBLE_TEST_ASSERT(kTest, local.model.value().view() == ::fixy::TextView{"ConnectX-6 Dx"});
    CRUCIBLE_TEST_ASSERT(kTest, local.driver.value().view() == ::fixy::TextView{"mlx5_core"});
    CRUCIBLE_TEST_ASSERT(kTest, local.firmware.value().view() == ::fixy::TextView{"22.39.1002"});
    CRUCIBLE_TEST_ASSERT(kTest, local.bus_info.value().view() == ::fixy::TextView{"0000:65:00.1"});
    CRUCIBLE_TEST_ASSERT(kTest, peer.model.value().view() == ::fixy::TextView{"tor-a"});
    CRUCIBLE_TEST_ASSERT(kTest, peer.bus_info.value().view() == ::fixy::TextView{"ifname swp17"});
    CRUCIBLE_TEST_ASSERT(kTest,
                         element_at(snapshot.nodes(), 0).vendor.value() == ::fixy::TextView{"Mellanox Technologies"});
    CRUCIBLE_TEST_ASSERT(kTest, element_at(snapshot.nodes(), 0).model.value() == ::fixy::TextView{"ConnectX-6 Dx"});
    CRUCIBLE_TEST_ASSERT(kTest, element_at(snapshot.nodes(), 1).model.value() == ::fixy::TextView{"tor-a"});
    ::fixy::check::pass(kTest, "  test_vendor_texts_outlive_their_input: PASSED\n");
}

// True when the view names the characters of the vendor text of a fact: the
// same first character and the same count.  The text is not empty.
[[nodiscard]] static bool names_text_of(cog::VendorClaim<::fixy::TextView> const& view,
                                        topology::VendorDiscoveryString const& text) {
    ::fixy::TextView const named = view.value();
    ::fixy::TextView const held = text.value().view();
    return named.size() == held.size() && held.size() != 0
        && ::fixy::address_of(::fixy::to(*named.begin())) == ::fixy::address_of(::fixy::to(*held.begin()));
}

// Each vendor and model view of each identity names the text of the fact at
// the same index of the snapshot, so it lies in the storage of that snapshot
// and in no text of a tool.  An update of a node points the views at the new
// texts of its fact.
static void test_identity_texts_name_the_facts_of_their_snapshot() {
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
                                       "Device:\tConnectX-6 Dx\n";
    static_cast<void>(topology::parse_lspci_vmm_tree(topology::tag_external_discovery_text(lspci), snapshot)
                          .expect("the lspci tree parses"));
    constexpr ::fixy::TextView info = "driver: mlx5_core\n"
                                      "firmware-version: 22.39.1002\n";
    static_cast<void>(topology::parse_ethtool_info(topology::tag_external_discovery_text(info), snapshot, 1)
                          .expect("the driver info parses"));
    CRUCIBLE_TEST_ASSERT(kTest, snapshot.node_count() == 2);
    for (std::size_t position = 0; position < snapshot.node_count(); ++position) {
        cog::CogIdentity const& identity = element_at(snapshot.nodes(), position);
        topology::DiscoveryNodeFact const& fact = element_at(snapshot.node_facts(), position);
        CRUCIBLE_TEST_ASSERT(kTest, names_text_of(identity.vendor, fact.vendor));
        CRUCIBLE_TEST_ASSERT(kTest, names_text_of(identity.model, fact.model));
    }
    ::fixy::check::pass(kTest, "  test_identity_texts_name_the_facts_of_their_snapshot: PASSED\n");
}

// A text of vendor_discovery_text_capacity characters fits a fact, and one
// more character refuses its record: the parse goes on, the record counts as
// seen and not as admitted, and the status is Partial.  A refused first
// record of lspci leaves node 0 to the next record.  A refused field of
// ethtool keeps the text that the fact held.
static void test_text_past_its_capacity_refuses_its_record() {
    eff::ColdInitCtx ctx{::foundation::effects::testing::init()};
    auto snapshot = topology::mint_discovery_snapshot(ctx);
    ::fixy::GrowText lspci = ::fixy::mint_grow_text(2 * topology::vendor_discovery_text_capacity + 256);
    auto const alloc = ::foundation::effects::testing::test().alloc;
    lspci.append(alloc, ::fixy::TextView{"Slot:\t0000:00:00.0\nDevice:\t"}).expect("the text fits its maximum");
    for (std::size_t count = 0; count <= topology::vendor_discovery_text_capacity; ++count) {
        lspci.append(alloc, 'x').expect("the text fits its maximum");
    }
    lspci.append(alloc, ::fixy::TextView{"\n\nSlot:\t0000:65:00.0\nDevice:\t"}).expect("the text fits its maximum");
    for (std::size_t count = 0; count < topology::vendor_discovery_text_capacity; ++count) {
        lspci.append(alloc, 'y').expect("the text fits its maximum");
    }
    lspci.append(alloc, ::fixy::TextView{"\n"}).expect("the text fits its maximum");
    ::fixy::HeapText const lspci_text = ::fixy::freeze(std::move(lspci));
    auto const lspci_status =
        topology::parse_lspci_vmm_tree(topology::tag_external_discovery_text(lspci_text.view()), snapshot)
            .expect("the lspci tree parses");
    CRUCIBLE_TEST_ASSERT(kTest, lspci_status.records_seen == 2 && lspci_status.records_admitted == 1);
    CRUCIBLE_TEST_ASSERT(kTest, lspci_status.outcome == topology::DiscoveryOutcome::Partial);
    CRUCIBLE_TEST_ASSERT(kTest, snapshot.node_count() == 1 && snapshot.edge_count() == 0);
    topology::DiscoveryNodeFact const& node = element_at(snapshot.node_facts(), 0);
    CRUCIBLE_TEST_ASSERT(kTest, node.bus_info.value().view() == ::fixy::TextView{"0000:65:00.0"});
    CRUCIBLE_TEST_ASSERT(kTest, node.model.value().size() == topology::vendor_discovery_text_capacity);

    ::fixy::GrowText info = ::fixy::mint_grow_text(topology::vendor_discovery_text_capacity + 64);
    info.append(alloc, ::fixy::TextView{"driver: mlx5_core\nfirmware-version: "}).expect("the text fits its maximum");
    for (std::size_t count = 0; count <= topology::vendor_discovery_text_capacity; ++count) {
        info.append(alloc, '9').expect("the text fits its maximum");
    }
    info.append(alloc, ::fixy::TextView{"\n"}).expect("the text fits its maximum");
    ::fixy::HeapText const info_text = ::fixy::freeze(std::move(info));
    auto const info_status =
        topology::parse_ethtool_info(topology::tag_external_discovery_text(info_text.view()), snapshot, 0)
            .expect("the driver info parses");
    CRUCIBLE_TEST_ASSERT(kTest, info_status.records_seen == 2 && info_status.records_admitted == 1);
    CRUCIBLE_TEST_ASSERT(kTest, info_status.outcome == topology::DiscoveryOutcome::Partial);
    CRUCIBLE_TEST_ASSERT(kTest,
                         element_at(snapshot.node_facts(), 0).driver.value().view() == ::fixy::TextView{"mlx5_core"});
    CRUCIBLE_TEST_ASSERT(kTest, element_at(snapshot.node_facts(), 0).firmware.value().size() == 0);

    ::fixy::GrowText lldp = ::fixy::mint_grow_text(topology::vendor_discovery_text_capacity + 128);
    lldp.append(alloc, ::fixy::TextView{"Interface: eth0\nSysName: tor-a\nPortID: "})
        .expect("the text fits its maximum");
    for (std::size_t count = 0; count <= topology::vendor_discovery_text_capacity; ++count) {
        lldp.append(alloc, 'p').expect("the text fits its maximum");
    }
    lldp.append(alloc, ::fixy::TextView{"\n\nInterface: eth1\nSysName: tor-b\nPortID: swp18\n"})
        .expect("the text fits its maximum");
    ::fixy::HeapText const lldp_text = ::fixy::freeze(std::move(lldp));
    auto const lldp_status =
        topology::parse_lldp_neighbors(topology::tag_external_discovery_text(lldp_text.view()), snapshot)
            .expect("the neighbour list parses");
    CRUCIBLE_TEST_ASSERT(kTest, lldp_status.records_seen == 2 && lldp_status.records_admitted == 1);
    CRUCIBLE_TEST_ASSERT(kTest, lldp_status.outcome == topology::DiscoveryOutcome::Partial);
    CRUCIBLE_TEST_ASSERT(kTest, snapshot.node_count() == 2);
    CRUCIBLE_TEST_ASSERT(kTest, element_at(snapshot.node_facts(), 1).model.value().view() == ::fixy::TextView{"tor-b"});
    ::fixy::check::pass(kTest, "  test_text_past_its_capacity_refuses_its_record: PASSED\n");
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
    test_vendor_texts_outlive_their_input();
    test_identity_texts_name_the_facts_of_their_snapshot();
    test_text_past_its_capacity_refuses_its_record();
    test_static_gates();
    test_snapshot_elements_have_no_padding_byte();
    ::fixy::check::pass(kTest, "test_topology_discovery: all PASSED\n");
    return 0;
}
