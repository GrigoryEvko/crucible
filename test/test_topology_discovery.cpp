#include <crucible/topology/Discovery.h>

#include <cassert>
#include <cstdio>
#include <string_view>
#include <type_traits>

namespace topology = crucible::topology;
namespace effects = crucible::effects;
namespace cog = crucible::cog;

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

static void test_name_coverage() {
    assert(topology::discovery_error_name(topology::DiscoveryError::TooManyNodes) ==
           std::string_view{"TooManyNodes"});
    assert(topology::discovery_source_name(topology::DiscoverySource::EthtoolInfo) ==
           std::string_view{"EthtoolInfo"});
    assert(topology::discovery_outcome_name(topology::DiscoveryOutcome::Partial) ==
           std::string_view{"Partial"});
    assert(topology::discovery_node_kind_name(topology::DiscoveryNodeKind::NicPort) ==
           std::string_view{"NicPort"});
    std::printf("  test_name_coverage:                  PASSED\n");
}

static void test_lspci_and_graph_materialization() {
    InitCtx ctx{};
    auto snapshot = topology::DefaultDiscoverySnapshot{};
    constexpr std::string_view lspci =
        "Slot:\t0000:00:00.0\n"
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

    auto status = topology::parse_lspci_vmm_tree(
        topology::tag_external_discovery_text(lspci), snapshot);
    assert(status.has_value());
    assert(status->records_seen == 3);
    assert(status->records_admitted == 3);
    assert(snapshot.node_count() == 3);
    assert(snapshot.edge_count() == 2);
    assert(snapshot.nodes()[0].kind == cog::CogKind::PcieRoot);
    assert(snapshot.nodes()[1].kind == cog::CogKind::NicPort);
    assert(snapshot.nodes()[2].kind == cog::CogKind::Gpu);

    auto graph = snapshot.graph(ctx);
    assert(graph.node_count() == 3);
    assert(graph.edge_count() == 2);
    auto edge = graph.edge_by_id(topology::EdgeId{0});
    assert(edge != nullptr);
    assert(edge->kind == topology::LinkKind::PciE);
    assert(edge->peer == &snapshot.nodes()[1]);
    std::printf("  test_lspci_and_graph_materialization: PASSED\n");
}

static void test_ethtool_features_and_lldp() {
    InitCtx ctx{};
    auto snapshot = topology::DefaultDiscoverySnapshot{};
    topology::DiscoveryNodeFact local{
        .kind = cog::CogKind::NicPort,
        .vendor = topology::tag_vendor_discovery_string("Mellanox"),
        .model = topology::tag_vendor_discovery_string("ConnectX-6"),
        .bus_info = topology::tag_vendor_discovery_string("0000:65:00.0"),
    };
    auto local_idx = snapshot.add_node(local);
    assert(local_idx.has_value());

    constexpr std::string_view info =
        "driver: mlx5_core\n"
        "firmware-version: 22.39.1002\n"
        "bus-info: 0000:65:00.0\n";
    auto info_status = topology::parse_ethtool_info(
        topology::tag_external_discovery_text(info), snapshot, *local_idx);
    assert(info_status.has_value());
    assert(snapshot.node_facts()[*local_idx].driver.value() ==
           std::string_view{"mlx5_core"});
    assert(snapshot.node_facts()[*local_idx].firmware.value() ==
           std::string_view{"22.39.1002"});

    constexpr std::string_view features =
        "tcp-segmentation-offload: on\n"
        "generic-segmentation-offload: on\n"
        "generic-receive-offload: on\n"
        "large-receive-offload: off\n"
        "tls-hw-tx-offload: on\n"
        "hw-tc-offload: on\n";
    auto bits = topology::parse_ethtool_features(
        topology::tag_external_discovery_text(features));
    assert(bits.has_value());
    assert(bits->test(cog::NicFeature::Tso));
    assert(bits->test(cog::NicFeature::Gso));
    assert(bits->test(cog::NicFeature::Gro));
    assert(!bits->test(cog::NicFeature::Lro));
    assert(bits->test(cog::NicFeature::KtlsOffload));
    assert(bits->test(cog::NicFeature::TcEbpf));

    constexpr std::string_view lldp =
        "Interface: eth0\n"
        "LineRate: 100G\n"
        "SysName: tor-a\n"
        "PortID: swp17\n";
    auto status = topology::parse_lldp_neighbors(
        topology::tag_external_discovery_text(lldp), snapshot);
    assert(status.has_value());
    assert(status->records_admitted == 1);
    assert(snapshot.node_count() == 2);
    assert(snapshot.edge_count() == 1);
    auto graph = snapshot.graph(ctx);
    assert(graph.edges()[0].kind == topology::LinkKind::Ethernet);
    assert(graph.edges()[0].bandwidth_bytes_per_sec.value() == 12'500'000'000ull);
    std::printf("  test_ethtool_features_and_lldp:       PASSED\n");
}

static void test_graceful_empty_live_discovery() {
    InitCtx ctx{};
    auto snapshot = topology::mint_discovery_snapshot<4, 4>(ctx);
    auto result = topology::discover_local_topology(ctx, snapshot);
    assert(result.node_count() == 0);
    assert(snapshot.report().view().size() == 1);
    assert(snapshot.report().view()[0].outcome == topology::DiscoveryOutcome::NotAttempted);

    BgCtx bg{};
    auto trigger = topology::notify_rediscovery_trigger(
        bg, topology::DiscoverySource::Udev);
    assert(trigger.has_value());
    assert(trigger->source == topology::DiscoverySource::Udev);
    std::printf("  test_graceful_empty_live_discovery:   PASSED\n");
}

static void test_static_gates() {
    static_assert(topology::CtxFitsDiscoveryInit<InitCtx>);
    static_assert(!topology::CtxFitsDiscoveryInit<BgCtx>);
    static_assert(topology::CtxFitsDiscoveryBg<BgCtx>);
    static_assert(!topology::DiscoveryShape<0, 1>);
    static_assert(topology::DiscoveryShape<1, 1>);
    static_assert(sizeof(topology::ExternalDiscoveryText) == sizeof(std::string_view));
    static_assert(std::is_trivially_destructible_v<
        topology::DiscoverySnapshot<16, 32>>);
    std::printf("  test_static_gates:                    PASSED\n");
}

int main() {
    std::printf("test_topology_discovery:\n");
    test_name_coverage();
    test_lspci_and_graph_materialization();
    test_ethtool_features_and_lldp();
    test_graceful_empty_live_discovery();
    test_static_gates();
    std::printf("test_topology_discovery: all PASSED\n");
    return 0;
}
