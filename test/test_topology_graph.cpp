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

#include "test_assert.h"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <type_traits>

namespace topology = crucible::topology;
namespace cog = crucible::cog;
namespace eff = ::fixy;

// The log spelling of the three enums comes from reflection.  The pins
// hold the spellings a log reader matches on, and the counts the size of
// each enum; a new enumerator moves a count and fails here.
static void test_enum_names_come_from_reflection() {
    using ::foundation::reflect::enum_count;
    using ::foundation::reflect::enum_name;
    static_assert(enum_count<topology::LinkKind> == 13);
    static_assert(enum_count<topology::LinkLayer> == 3);
    static_assert(enum_count<topology::CongestionState> == 5);
    static_assert(enum_name(topology::LinkKind::AmdInfinityFabric) == "AmdInfinityFabric");
    static_assert(enum_name(topology::LinkLayer::L3) == "L3");
    static_assert(enum_name(topology::CongestionState::Saturated) == "Saturated");

    volatile auto kind = topology::LinkKind::RoceV2;
    assert(enum_name(static_cast<topology::LinkKind>(kind)) == std::string_view{"RoceV2"});
    crucible::test::pass("  test_enum_names_come_from_reflection: PASSED\n");
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
        assert(got == p.expected);
    }
    crucible::test::pass("  test_link_layer_for_runtime:          PASSED\n");
}

static void test_edge_id_runtime() {
    volatile std::uint32_t raw = UINT32_MAX;
    topology::EdgeId none{static_cast<std::uint32_t>(raw)};
    assert(none.is_none());
    assert(topology::EdgeId::none() == none);

    volatile std::uint32_t real_raw = 42;
    topology::EdgeId real{static_cast<std::uint32_t>(real_raw)};
    assert(!real.is_none());
    assert(real.raw() == 42);

    // The comparison operators come from a defaulted three-way
    // comparison, so all three below resolve through one function.
    topology::EdgeId a{1}, b{2};
    assert(a < b);
    assert(b > a);
    assert(a != b);

    crucible::test::pass("  test_edge_id_runtime:                 PASSED\n");
}

static void test_default_topology_edge() {
    static_assert([] {
        topology::TopologyEdge const zero{};
        return zero.id.is_none() && zero.kind == topology::LinkKind::Unknown
            && zero.state == topology::CongestionState::Healthy && zero.peer.is_none()
            && zero.bandwidth_bytes_per_sec.value() == 0 && zero.rtt_ns_p50.value() == 0 && zero.rtt_ns_p99.value() == 0
            && std::bit_cast<std::uint32_t>(zero.drop_rate.value()) == 0u;
    }());

    topology::TopologyEdge e{};
    assert(e.id.is_none());
    assert(e.kind == topology::LinkKind::Unknown);
    assert(e.state == topology::CongestionState::Healthy);
    assert(e.peer.is_none());
    assert(e.bandwidth_bytes_per_sec.value() == 0);
    assert(e.rtt_ns_p50.value() == 0);
    assert(e.rtt_ns_p99.value() == 0);
    // Comparing the bits rather than the values, because comparing
    // floating-point values for equality is rejected outright.
    assert(std::bit_cast<std::uint32_t>(e.drop_rate.value()) == 0u);

    // The edge occupies exactly one cache line.
    static_assert(sizeof(topology::TopologyEdge) == 64);
    crucible::test::pass("  test_default_topology_edge:           PASSED\n");
}

static void test_mint_topology_graph_round_trip() {
    // Three nodes in a line, joined by two links.  Each link is stored
    // as a pair of half-edges, one per direction, which is where the
    // four edges come from.
    using ::fixy::index;
    ::foundation::core::FixedArray<cog::CogIdentity, 3> nodes{};
    nodes.at(index<0>()).uuid = cog::Uuid{0x1ULL, 0x1ULL};
    nodes.at(index<0>()).kind = cog::CogKind::Gpu;
    nodes.at(index<1>()).uuid = cog::Uuid{0x2ULL, 0x2ULL};
    nodes.at(index<1>()).kind = cog::CogKind::NicPort;
    nodes.at(index<2>()).uuid = cog::Uuid{0x3ULL, 0x3ULL};
    nodes.at(index<2>()).kind = cog::CogKind::NvSwitch;

    ::foundation::core::FixedArray<topology::TopologyEdge, 4> edges{};
    edges.at(index<0>()).id = topology::EdgeId{0};
    edges.at(index<0>()).kind = topology::LinkKind::PciE;
    edges.at(index<0>()).peer = topology::NodeId{1};
    edges.at(index<0>()).bandwidth_bytes_per_sec =
        ::fixy::mint_tagged<::fixy::tags::source::Calibrated>(std::uint64_t{32} * 1024 * 1024 * 1024);

    edges.at(index<1>()).id = topology::EdgeId{1};
    edges.at(index<1>()).kind = topology::LinkKind::PciE;
    edges.at(index<1>()).peer = topology::NodeId{0};  // reverse half-edge

    edges.at(index<2>()).id = topology::EdgeId{2};
    edges.at(index<2>()).kind = topology::LinkKind::Ethernet;
    edges.at(index<2>()).peer = topology::NodeId{2};

    edges.at(index<3>()).id = topology::EdgeId{3};
    edges.at(index<3>()).kind = topology::LinkKind::Ethernet;
    edges.at(index<3>()).peer = topology::NodeId{1};

    eff::ColdInitCtx ctx{::foundation::effects::testing::init()};
    auto g = topology::mint_topology_graph(ctx, nodes.view(), edges.view());

    volatile auto nc = g.node_count();
    volatile auto ec = g.edge_count();
    assert(nc == 3);
    assert(ec == 4);
    assert(g.nodes().size() == 3);
    assert(g.edges().size() == 4);

    topology::TopologyEdge const e0 = g.edge_by_id(topology::EdgeId{0}).expect("the graph holds edge 0");
    assert(e0.kind == topology::LinkKind::PciE);
    assert(e0.peer == topology::NodeId{1});

    topology::TopologyEdge const e3 = g.edge_by_id(topology::EdgeId{3}).expect("the graph holds edge 3");
    assert(e3.peer == topology::NodeId{1});

    assert(g.edge_by_id(topology::EdgeId{42}).is_none());

    ::foundation::core::FixedArray<topology::EdgeId, 8> incident{};
    auto found = g.edges_incident_on(topology::NodeId{1}, incident.view());
    // The middle node is named as the far end of both links, once
    // from each side.
    assert(found == 2);
    assert(incident.at(index<0>()) == topology::EdgeId{0});
    assert(incident.at(index<1>()) == topology::EdgeId{3});

    auto found_on_end_node = g.edges_incident_on(topology::NodeId{0}, incident.view());
    // An end node is named by one half-edge only.
    assert(found_on_end_node == 1);
    assert(incident.at(index<0>()) == topology::EdgeId{1});

    crucible::test::pass("  test_mint_topology_graph_round_trip:  PASSED\n");
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
    assert(nc == 0);
    assert(ec == 0);
    crucible::test::pass("  test_empty_topology_graph:            PASSED\n");
}

// Each concept result is captured into a volatile so the compiler
// cannot fold the branch away and skip evaluating it.
static void test_ctx_fits_topology_graph_runtime() {
    volatile bool init_admits = topology::CtxFitsTopologyGraph<eff::ColdInitCtx>;
    volatile bool bg_admits = topology::CtxFitsTopologyGraph<eff::BgDrainCtx>;
    volatile bool test_admits = topology::CtxFitsTopologyGraph<eff::TestRunnerCtx>;
    volatile bool int_admits = topology::CtxFitsTopologyGraph<int>;

    // Only a context carrying the initialization effect is admitted.
    assert(init_admits);
    assert(!bg_admits);
    assert(!test_admits);
    // A type that is not a context at all is refused by the other
    // half of the gate.
    assert(!int_admits);

    crucible::test::pass("  test_ctx_fits_topology_graph_runtime: PASSED\n");
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
    crucible::test::pass("  test_topology_graph_pinned:           PASSED\n");
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
    crucible::test::pass("test_topology_graph: all PASSED\n");
    return 0;
}
