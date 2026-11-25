// Assertions embedded in a header are only verified under the
// project's warning flags when some translation unit includes that
// header.  This file exists to be that translation unit for the
// topology graph.
//
// Every accessor is additionally called with arguments the compiler
// cannot fold, so that a wrong switch arm or a pointer-arithmetic
// slip shows on the runtime path and not only in the constant
// evaluator.

#include <crucible/topology/TopologyGraph.h>
#include <foundation/reflect/EnumName.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include <bit>
#include <cstdint>
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

// The log spelling of the three enums comes from reflection.  The pins
// hold the spellings a log reader matches on, and the counts the size of
// each enum; a new enumerator moves a count and fails here.
static void test_enum_names_come_from_reflection() {
    using ::foundation::reflect::enum_count;
    using ::fixy::enum_name_text;
    static_assert(enum_count<topology::LinkKind> == 13);
    static_assert(enum_count<topology::LinkLayer> == 3);
    static_assert(enum_count<topology::CongestionState> == 5);
    static_assert(enum_name_text(topology::LinkKind::AmdInfinityFabric) == "AmdInfinityFabric");
    static_assert(enum_name_text(topology::LinkLayer::L3) == "L3");
    static_assert(enum_name_text(topology::CongestionState::Saturated) == "Saturated");

    volatile auto kind = topology::LinkKind::RoceV2;
    CRUCIBLE_TEST_ASSERT(kTest, enum_name_text(static_cast<topology::LinkKind>(kind)) == ::fixy::TextView{"RoceV2"});
    ::fixy::check::pass(kTest, "  test_enum_names_come_from_reflection: PASSED\n");
}

// The mapping from link kind to layer is a partition, so every kind
// has exactly one answer and the table below is exhaustive.
static void test_link_layer_for_runtime() {
    using LK = topology::LinkKind;
    using LL = topology::LinkLayer;

    struct Pair {
        LK kind;
        LL expected;
    };
    constexpr Pair pairs[] = {
        {LK::Unknown, LL::Unknown},
        {LK::PciE, LL::L2},
        {LK::NvLink, LL::L2},
        {LK::NvSwitchPort, LL::L2},
        {LK::AmdInfinityFabric, LL::L2},
        {LK::CxlMem, LL::L2},
        {LK::CxlCache, LL::L2},
        {LK::QpiUpi, LL::L2},
        {LK::Cxio, LL::L2},
        {LK::Ethernet, LL::L3},
        {LK::Infiniband, LL::L3},
        {LK::RoceV2, LL::L3},
        {LK::Loopback, LL::L3},
    };

    for (auto const& p : pairs) {
        volatile auto v = p.kind;
        auto got = topology::link_layer_for(static_cast<topology::LinkKind>(v));
        CRUCIBLE_TEST_ASSERT(kTest, got == p.expected);
    }
    ::fixy::check::pass(kTest, "  test_link_layer_for_runtime:          PASSED\n");
}

static void test_edge_id_runtime() {
    volatile std::uint32_t raw = UINT32_MAX;
    topology::EdgeId none{static_cast<std::uint32_t>(raw)};
    CRUCIBLE_TEST_ASSERT(kTest, none.is_none());
    CRUCIBLE_TEST_ASSERT(kTest, topology::EdgeId::none() == none);

    volatile std::uint32_t real_raw = 42;
    topology::EdgeId real{static_cast<std::uint32_t>(real_raw)};
    CRUCIBLE_TEST_ASSERT(kTest, !real.is_none());
    CRUCIBLE_TEST_ASSERT(kTest, real.raw() == 42);

    // The comparison operators come from a defaulted three-way
    // comparison, so all three below resolve through one function.
    topology::EdgeId a{1}, b{2};
    CRUCIBLE_TEST_ASSERT(kTest, a < b);
    CRUCIBLE_TEST_ASSERT(kTest, b > a);
    CRUCIBLE_TEST_ASSERT(kTest, a != b);

    ::fixy::check::pass(kTest, "  test_edge_id_runtime:                 PASSED\n");
}

static void test_default_topology_edge() {
    static_assert([] {
        topology::TopologyEdge const zero{};
        return zero.id.is_none() && zero.kind == topology::LinkKind::Unknown
            && zero.state == topology::CongestionState::Healthy && zero.peer.is_none()
            && zero.bandwidth_bytes_per_sec.value() == 0 && zero.rtt_ns_p50.value() == 0 && zero.rtt_ns_p99.value() == 0
            && ::fixy::bit_cast<std::uint32_t>(zero.drop_rate.value()) == 0u;
    }());

    topology::TopologyEdge e{};
    CRUCIBLE_TEST_ASSERT(kTest, e.id.is_none());
    CRUCIBLE_TEST_ASSERT(kTest, e.kind == topology::LinkKind::Unknown);
    CRUCIBLE_TEST_ASSERT(kTest, e.state == topology::CongestionState::Healthy);
    CRUCIBLE_TEST_ASSERT(kTest, e.peer.is_none());
    CRUCIBLE_TEST_ASSERT(kTest, e.bandwidth_bytes_per_sec.value() == 0);
    CRUCIBLE_TEST_ASSERT(kTest, e.rtt_ns_p50.value() == 0);
    CRUCIBLE_TEST_ASSERT(kTest, e.rtt_ns_p99.value() == 0);
    // Comparing the bits rather than the values, because comparing
    // floating-point values for equality is rejected outright.
    CRUCIBLE_TEST_ASSERT(kTest, ::fixy::bit_cast<std::uint32_t>(e.drop_rate.value()) == 0u);

    // The edge occupies exactly one cache line.
    static_assert(sizeof(topology::TopologyEdge) == 64);
    ::fixy::check::pass(kTest, "  test_default_topology_edge:           PASSED\n");
}

static void test_mint_topology_graph_round_trip() {
    // Three nodes in a line, joined by two links.  Each link is stored
    // as a pair of half-edges, one per direction, which is where the
    // four edges come from.
    using ::fixy::index;
    ::foundation::core::FixedArray<cog::CogIdentity, 3> nodes{};
    nodes.update(index<0>(), [](cog::CogIdentity& node) noexcept {
        node.uuid = cog::Uuid{0x1ULL, 0x1ULL};
        node.kind = cog::CogKind::Gpu;
    });
    nodes.update(index<1>(), [](cog::CogIdentity& node) noexcept {
        node.uuid = cog::Uuid{0x2ULL, 0x2ULL};
        node.kind = cog::CogKind::NicPort;
    });
    nodes.update(index<2>(), [](cog::CogIdentity& node) noexcept {
        node.uuid = cog::Uuid{0x3ULL, 0x3ULL};
        node.kind = cog::CogKind::NvSwitch;
    });

    ::foundation::core::FixedArray<topology::TopologyEdge, 4> edges{};
    auto const bandwidth =
        ::fixy::mint_tagged<::fixy::tags::source::Calibrated>(std::uint64_t{32} * 1024 * 1024 * 1024);
    edges.update(index<0>(), [bandwidth](topology::TopologyEdge& edge) noexcept {
        edge.id = topology::EdgeId{0};
        edge.kind = topology::LinkKind::PciE;
        edge.peer = topology::NodeId{1};
        edge.bandwidth_bytes_per_sec = bandwidth;
    });

    edges.update(index<1>(), [](topology::TopologyEdge& edge) noexcept {
        edge.id = topology::EdgeId{1};
        edge.kind = topology::LinkKind::PciE;
        edge.peer = topology::NodeId{0};  // reverse half-edge
    });

    edges.update(index<2>(), [](topology::TopologyEdge& edge) noexcept {
        edge.id = topology::EdgeId{2};
        edge.kind = topology::LinkKind::Ethernet;
        edge.peer = topology::NodeId{2};
    });

    edges.update(index<3>(), [](topology::TopologyEdge& edge) noexcept {
        edge.id = topology::EdgeId{3};
        edge.kind = topology::LinkKind::Ethernet;
        edge.peer = topology::NodeId{1};
    });

    eff::ColdInitCtx ctx{::foundation::effects::testing::init()};
    auto g = topology::mint_topology_graph(ctx, nodes.view(), edges.view());

    volatile auto nc = g.node_count();
    volatile auto ec = g.edge_count();
    CRUCIBLE_TEST_ASSERT(kTest, nc == 3);
    CRUCIBLE_TEST_ASSERT(kTest, ec == 4);
    CRUCIBLE_TEST_ASSERT(kTest, g.nodes().size() == 3);
    CRUCIBLE_TEST_ASSERT(kTest, g.edges().size() == 4);

    topology::TopologyEdge const e0 = g.edge_by_id(topology::EdgeId{0}).expect("the graph holds edge 0");
    CRUCIBLE_TEST_ASSERT(kTest, e0.kind == topology::LinkKind::PciE);
    CRUCIBLE_TEST_ASSERT(kTest, e0.peer == topology::NodeId{1});

    topology::TopologyEdge const e3 = g.edge_by_id(topology::EdgeId{3}).expect("the graph holds edge 3");
    CRUCIBLE_TEST_ASSERT(kTest, e3.peer == topology::NodeId{1});

    CRUCIBLE_TEST_ASSERT(kTest, g.edge_by_id(topology::EdgeId{42}).is_none());

    auto [incident, incident_key] =
        ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<topology::EdgeId, 8>);
    auto const& g_read = g;
    auto found = incident.with_mut(incident_key, [&g_read](auto run) noexcept {
        return g_read.edges_incident_on(topology::NodeId{1}, std::move(run));
    });
    // The middle node is named as the far end of both links, once
    // from each side.
    CRUCIBLE_TEST_ASSERT(kTest, found == 2);
    CRUCIBLE_TEST_ASSERT(kTest, incident.at(index<0>()) == topology::EdgeId{0});
    CRUCIBLE_TEST_ASSERT(kTest, incident.at(index<1>()) == topology::EdgeId{3});

    auto found_on_end_node = incident.with_mut(incident_key, [&g_read](auto run) noexcept {
        return g_read.edges_incident_on(topology::NodeId{0}, std::move(run));
    });
    // An end node is named by one half-edge only.
    CRUCIBLE_TEST_ASSERT(kTest, found_on_end_node == 1);
    CRUCIBLE_TEST_ASSERT(kTest, incident.at(index<0>()) == topology::EdgeId{1});

    ::fixy::check::pass(kTest, "  test_mint_topology_graph_round_trip:  PASSED\n");
}

// A graph minted over no storage reports no nodes and no edges, in the
// constant evaluator and at run time.
static void test_empty_topology_graph() {
    static_assert([] {
        eff::ColdInitCtx ctx{::foundation::effects::testing::init()};
        auto g = topology::mint_topology_graph(ctx, ::fixy::View<cog::CogIdentity const>{},
                                               ::fixy::View<topology::TopologyEdge const>{});
        return g.node_count() == 0 && g.edge_count() == 0;
    }());

    eff::ColdInitCtx ctx{::foundation::effects::testing::init()};
    auto g = topology::mint_topology_graph(ctx, ::fixy::View<cog::CogIdentity const>{},
                                           ::fixy::View<topology::TopologyEdge const>{});
    volatile auto nc = g.node_count();
    volatile auto ec = g.edge_count();
    CRUCIBLE_TEST_ASSERT(kTest, nc == 0);
    CRUCIBLE_TEST_ASSERT(kTest, ec == 0);
    ::fixy::check::pass(kTest, "  test_empty_topology_graph:            PASSED\n");
}

// Each concept result is captured into a volatile so the compiler
// cannot fold the branch away and skip evaluating it.
static void test_ctx_fits_topology_graph_runtime() {
    volatile bool init_admits = topology::CtxFitsTopologyGraph<eff::ColdInitCtx>;
    volatile bool bg_admits = topology::CtxFitsTopologyGraph<eff::BgDrainCtx>;
    volatile bool test_admits = topology::CtxFitsTopologyGraph<eff::TestRunnerCtx>;
    volatile bool int_admits = topology::CtxFitsTopologyGraph<int>;

    // Only a context carrying the initialization effect is admitted.
    CRUCIBLE_TEST_ASSERT(kTest, init_admits);
    CRUCIBLE_TEST_ASSERT(kTest, !bg_admits);
    CRUCIBLE_TEST_ASSERT(kTest, !test_admits);
    // A type that is not a context at all is refused by the other
    // half of the gate.
    CRUCIBLE_TEST_ASSERT(kTest, !int_admits);

    ::fixy::check::pass(kTest, "  test_ctx_fits_topology_graph_runtime: PASSED\n");
}

static void test_topology_graph_pinned() {
    static_assert(!std::is_copy_constructible_v<topology::TopologyGraph>,
                  "The graph is one shared identity and must not be copied.");
    static_assert(!std::is_move_constructible_v<topology::TopologyGraph>,
                  "The graph must not be moved, or references cached into it "
                  "would dangle.");
    static_assert(!std::is_copy_assignable_v<topology::TopologyGraph>);
    static_assert(!std::is_move_assignable_v<topology::TopologyGraph>);
    static_assert(std::is_trivially_destructible_v<topology::TopologyGraph>,
                  "The graph holds only spans and owns no storage, so it must "
                  "destroy trivially.");
    ::fixy::check::pass(kTest, "  test_topology_graph_pinned:           PASSED\n");
}

int main() {
    ::fixy::report(::fixy::Sink::Out, "test_topology_graph:\n");
    test_enum_names_come_from_reflection();
    test_link_layer_for_runtime();
    test_edge_id_runtime();
    test_default_topology_edge();
    test_mint_topology_graph_round_trip();
    test_empty_topology_graph();
    test_ctx_fits_topology_graph_runtime();
    test_topology_graph_pinned();
    ::fixy::check::pass(kTest, "test_topology_graph: all PASSED\n");
    return 0;
}
