// Sentinel TU for include/crucible/topology/TopologyGraph.h.
//
// Per feedback_header_only_static_assert_blind_spot.md: in-header
// static_asserts (FOUND-I04 frozen-position pins on LinkKind /
// LinkLayer / CongestionState ordinals + reflection-driven name
// coverage + link_layer_for partition soundness + default-state
// semantics + CtxFitsTopologyGraph runtime-evaluable concept gate)
// fire only under TUs that include the header from the build graph.
// This sentinel makes the inclusion explicit so every default build
// exercises the in-header invariants under project warning flags.
//
// Per feedback_algebra_runtime_smoke_test_discipline: every accessor
// (link_kind_name, link_layer_name, congestion_state_name,
// link_layer_for, EdgeId::raw / is_none / none, TopologyGraph::nodes
// / edges / node_count / edge_count / edge_by_id / edges_incident_on,
// mint_topology_graph) is driven with non-constant runtime arguments
// here so a regression in a switch arm or pointer-arithmetic path
// surfaces under runtime semantics, not only at consteval time.
//
// GAPS-110.

#include <crucible/topology/TopologyGraph.h>

#include "test_assert.h"

#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string_view>
#include <type_traits>

namespace topology = crucible::topology;
namespace cog      = crucible::cog;
namespace effects  = crucible::effects;
namespace safety   = crucible::safety;

// ── Reflection-driven name coverage ─────────────────────────────────

static void test_link_kind_name_coverage() {
    constexpr topology::LinkKind kinds[] = {
        topology::LinkKind::Unknown,
        topology::LinkKind::PciE,
        topology::LinkKind::NvLink,
        topology::LinkKind::NvSwitchPort,
        topology::LinkKind::AmdInfinityFabric,
        topology::LinkKind::CxlMem,
        topology::LinkKind::CxlCache,
        topology::LinkKind::QpiUpi,
        topology::LinkKind::Cxio,
        topology::LinkKind::Ethernet,
        topology::LinkKind::Infiniband,
        topology::LinkKind::RoceV2,
        topology::LinkKind::Loopback,
    };
    static_assert(sizeof(kinds) / sizeof(kinds[0]) == topology::link_kind_count,
        "Manual LinkKind table diverged from link_kind_count.");

    for (topology::LinkKind k : kinds) {
        volatile auto v = k;
        std::string_view name = topology::link_kind_name(
            static_cast<topology::LinkKind>(v));
        assert(!name.empty());
        assert(name != std::string_view{"<unknown LinkKind>"});
    }
    std::printf("  test_link_kind_name_coverage:         PASSED\n");
}

static void test_link_layer_name_coverage() {
    constexpr topology::LinkLayer layers[] = {
        topology::LinkLayer::Unknown,
        topology::LinkLayer::L2,
        topology::LinkLayer::L3,
    };
    static_assert(sizeof(layers) / sizeof(layers[0]) == topology::link_layer_count);

    for (topology::LinkLayer L : layers) {
        volatile auto v = L;
        std::string_view name = topology::link_layer_name(
            static_cast<topology::LinkLayer>(v));
        assert(!name.empty());
        assert(name != std::string_view{"<unknown LinkLayer>"});
    }
    std::printf("  test_link_layer_name_coverage:        PASSED\n");
}

static void test_congestion_state_name_coverage() {
    constexpr topology::CongestionState states[] = {
        topology::CongestionState::Healthy,
        topology::CongestionState::Mild,
        topology::CongestionState::Severe,
        topology::CongestionState::Saturated,
        topology::CongestionState::Down,
    };
    static_assert(sizeof(states) / sizeof(states[0]) == topology::congestion_state_count);

    for (topology::CongestionState C : states) {
        volatile auto v = C;
        std::string_view name = topology::congestion_state_name(
            static_cast<topology::CongestionState>(v));
        assert(!name.empty());
        assert(name != std::string_view{"<unknown CongestionState>"});
    }
    std::printf("  test_congestion_state_name_coverage:  PASSED\n");
}

// ── link_layer_for runtime smoke ─────────────────────────────────────
//
// Drive every kind through link_layer_for at runtime (not consteval)
// so a regression in the partition arithmetic surfaces here.

static void test_link_layer_for_runtime() {
    using LK = topology::LinkKind;
    using LL = topology::LinkLayer;

    struct Pair { LK kind; LL expected; };
    constexpr Pair pairs[] = {
        { LK::Unknown,           LL::Unknown },
        { LK::PciE,              LL::L2 },
        { LK::NvLink,            LL::L2 },
        { LK::NvSwitchPort,      LL::L2 },
        { LK::AmdInfinityFabric, LL::L2 },
        { LK::CxlMem,            LL::L2 },
        { LK::CxlCache,          LL::L2 },
        { LK::QpiUpi,            LL::L2 },
        { LK::Cxio,              LL::L2 },
        { LK::Ethernet,          LL::L3 },
        { LK::Infiniband,        LL::L3 },
        { LK::RoceV2,            LL::L3 },
        { LK::Loopback,          LL::L3 },
    };

    for (auto const& p : pairs) {
        volatile auto v = p.kind;
        auto got = topology::link_layer_for(
            static_cast<topology::LinkKind>(v));
        assert(got == p.expected);
    }
    std::printf("  test_link_layer_for_runtime:          PASSED\n");
}

// ── EdgeId basic semantics at runtime ───────────────────────────────

static void test_edge_id_runtime() {
    volatile std::uint32_t raw = UINT32_MAX;
    topology::EdgeId none{static_cast<std::uint32_t>(raw)};
    assert(none.is_none());
    assert(topology::EdgeId::none() == none);

    volatile std::uint32_t real_raw = 42;
    topology::EdgeId real{static_cast<std::uint32_t>(real_raw)};
    assert(!real.is_none());
    assert(real.raw() == 42);

    // Operator <=> drives the spaceship comparator at runtime.
    topology::EdgeId a{1}, b{2};
    assert(a < b);
    assert(b > a);
    assert(a != b);

    std::printf("  test_edge_id_runtime:                 PASSED\n");
}

// ── Default TopologyEdge zero-state ─────────────────────────────────

static void test_default_topology_edge() {
    topology::TopologyEdge e{};
    assert(e.id.is_none());
    assert(e.kind == topology::LinkKind::Unknown);
    assert(e.state == topology::CongestionState::Healthy);
    assert(e.peer == nullptr);
    assert(e.bandwidth_bytes_per_sec.value() == 0);
    assert(e.rtt_ns_p50.value() == 0);
    assert(e.rtt_ns_p99.value() == 0);
    // Bit-equality on +0.0f (CLAUDE.md §VI -Werror=float-equal).
    assert(std::bit_cast<std::uint32_t>(e.drop_rate.value()) == 0u);

    // Padding bytes are zero (NSDMI guarantee).
    static_assert(sizeof(topology::TopologyEdge) == 64);
    std::printf("  test_default_topology_edge:           PASSED\n");
}

// ── mint_topology_graph round-trip ──────────────────────────────────

using InitCtx = effects::ExecCtx<
    effects::Init,
    effects::ctx_numa::Any,
    effects::ctx_alloc::Unbound,
    effects::ctx_heat::Cold,
    effects::ctx_resid::DRAM,
    effects::Row<effects::Effect::Init>,
    effects::ctx_workload::Unspecified>;

static void test_mint_topology_graph_round_trip() {
    // Build a 3-node, 4-edge graph: GPU --PCIe--> NIC --Ethernet--> Switch
    // plus reverse half-edges for symmetry.
    cog::CogIdentity nodes[3]{};
    nodes[0].uuid = cog::Uuid{0x1ULL, 0x1ULL};
    nodes[0].kind = cog::CogKind::Gpu;
    nodes[1].uuid = cog::Uuid{0x2ULL, 0x2ULL};
    nodes[1].kind = cog::CogKind::NicPort;
    nodes[2].uuid = cog::Uuid{0x3ULL, 0x3ULL};
    nodes[2].kind = cog::CogKind::NvSwitch;

    topology::TopologyEdge edges[4]{};
    edges[0].id   = topology::EdgeId{0};
    edges[0].kind = topology::LinkKind::PciE;
    edges[0].peer = &nodes[1];
    edges[0].bandwidth_bytes_per_sec =
        safety::Tagged<std::uint64_t, safety::source::Calibrated>{
            std::uint64_t{32} * 1024 * 1024 * 1024};

    edges[1].id   = topology::EdgeId{1};
    edges[1].kind = topology::LinkKind::PciE;
    edges[1].peer = &nodes[0];   // reverse half-edge

    edges[2].id   = topology::EdgeId{2};
    edges[2].kind = topology::LinkKind::Ethernet;
    edges[2].peer = &nodes[2];

    edges[3].id   = topology::EdgeId{3};
    edges[3].kind = topology::LinkKind::Ethernet;
    edges[3].peer = &nodes[1];

    InitCtx ctx{};
    auto g = topology::mint_topology_graph(
        ctx,
        std::span<const cog::CogIdentity>{nodes, 3},
        std::span<const topology::TopologyEdge>{edges, 4});

    // Counts.
    volatile auto nc = g.node_count();
    volatile auto ec = g.edge_count();
    assert(nc == 3);
    assert(ec == 4);
    assert(g.nodes().size() == 3);
    assert(g.edges().size() == 4);

    // edge_by_id round-trips.
    auto* e0 = g.edge_by_id(topology::EdgeId{0});
    assert(e0 != nullptr);
    assert(e0->kind == topology::LinkKind::PciE);
    assert(e0->peer == &nodes[1]);

    auto* e3 = g.edge_by_id(topology::EdgeId{3});
    assert(e3 != nullptr);
    assert(e3->peer == &nodes[1]);

    // Miss returns nullptr.
    auto* miss = g.edge_by_id(topology::EdgeId{42});
    assert(miss == nullptr);

    // edges_incident_on counts both directions.
    topology::TopologyEdge const* incident_buf[8]{};
    auto found = g.edges_incident_on(
        &nodes[1],
        std::span<topology::TopologyEdge const*>{incident_buf, 8});
    // nodes[1] (NIC) is the peer of edges[0] (GPU→NIC) and edges[3] (Switch→NIC).
    assert(found == 2);

    auto found_zero = g.edges_incident_on(
        &nodes[0],
        std::span<topology::TopologyEdge const*>{incident_buf, 8});
    // nodes[0] (GPU) is the peer of edges[1] (NIC→GPU) only.
    assert(found_zero == 1);

    std::printf("  test_mint_topology_graph_round_trip:  PASSED\n");
}

// ── CtxFitsTopologyGraph runtime-evaluable concept gate ─────────────
//
// Drive concept evaluation at runtime by capturing the bool into a
// volatile so the compiler can't fold the entire branch.

using BgCtx = effects::ExecCtx<
    effects::Bg,
    effects::ctx_numa::Any,
    effects::ctx_alloc::Arena,
    effects::ctx_heat::Warm,
    effects::ctx_resid::L3,
    effects::Row<effects::Effect::Bg, effects::Effect::Alloc>,
    effects::ctx_workload::Unspecified>;

using TestCtx = effects::ExecCtx<
    effects::Test,
    effects::ctx_numa::Any,
    effects::ctx_alloc::Unbound,
    effects::ctx_heat::Cold,
    effects::ctx_resid::DRAM,
    effects::Row<effects::Effect::Test>,
    effects::ctx_workload::Unspecified>;

static void test_ctx_fits_topology_graph_runtime() {
    volatile bool init_admits = topology::CtxFitsTopologyGraph<InitCtx>;
    volatile bool bg_admits   = topology::CtxFitsTopologyGraph<BgCtx>;
    volatile bool test_admits = topology::CtxFitsTopologyGraph<TestCtx>;
    volatile bool int_admits  = topology::CtxFitsTopologyGraph<int>;

    // Init-row ctx admits — the only one that does.
    assert(init_admits);
    // Bg-only ctx refused — Init not in row.
    assert(!bg_admits);
    // Test-row ctx refused — Init not in row.
    assert(!test_admits);
    // Bare int refused — IsExecCtx conjunct fires.
    assert(!int_admits);

    std::printf("  test_ctx_fits_topology_graph_runtime: PASSED\n");
}

// ── TopologyGraph copy/move discipline ──────────────────────────────

static void test_topology_graph_pinned() {
    static_assert(!std::is_copy_constructible_v<topology::TopologyGraph>,
        "TopologyGraph must be copy-deleted — fleet-shared identity.");
    static_assert(!std::is_move_constructible_v<topology::TopologyGraph>,
        "TopologyGraph must be move-deleted — cached const-refs would dangle.");
    static_assert(!std::is_copy_assignable_v<topology::TopologyGraph>);
    static_assert(!std::is_move_assignable_v<topology::TopologyGraph>);
    static_assert(std::is_trivially_destructible_v<topology::TopologyGraph>,
        "TopologyGraph holds only spans — trivial destruction expected.");
    std::printf("  test_topology_graph_pinned:           PASSED\n");
}

int main() {
    std::printf("test_topology_graph:\n");
    test_link_kind_name_coverage();
    test_link_layer_name_coverage();
    test_congestion_state_name_coverage();
    test_link_layer_for_runtime();
    test_edge_id_runtime();
    test_default_topology_edge();
    test_mint_topology_graph_round_trip();
    test_ctx_fits_topology_graph_runtime();
    test_topology_graph_pinned();
    std::printf("test_topology_graph: all PASSED\n");
    return 0;
}
