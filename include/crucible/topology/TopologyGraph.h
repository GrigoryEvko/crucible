#pragma once

#include <crucible/Platform.h>
#include <crucible/cog/CogIdentity.h>
#include <crucible/effects/_Capabilities.h>
#include <crucible/effects/_EffectRow.h>
#include <crucible/effects/_ExecCtx.h>
#include <crucible/safety/Tagged.h>

#include <bit>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::topology {

struct EdgeId {
    std::uint32_t value_ = UINT32_MAX;

    constexpr EdgeId() noexcept = default;
    explicit constexpr EdgeId(std::uint32_t v) noexcept : value_{v} {}

    [[nodiscard]] constexpr std::uint32_t raw() const noexcept { return value_; }
    [[nodiscard]] constexpr bool is_none() const noexcept { return value_ == UINT32_MAX; }
    [[nodiscard]] static constexpr EdgeId none() noexcept { return EdgeId{UINT32_MAX}; }

    constexpr auto operator<=>(EdgeId const&) const = default;
};

static_assert(sizeof(EdgeId) == sizeof(std::uint32_t),
              "EdgeId must collapse to a bare uint32_t at runtime — strong ID is "
              "phantom-typed at compile time only.");
static_assert(std::is_trivially_destructible_v<EdgeId>);
static_assert(std::is_trivially_copyable_v<EdgeId>);

// The underlying value encodes the layer by range.  Zero is the unknown
// sentinel, 1 through 15 are intra-node hardware links, and 16 through 31 are
// inter-node network links.  link_layer_for projects a kind onto a layer from
// that range alone.  A new kind takes the next free value inside the range for
// its own layer.
enum class LinkKind : std::uint8_t {
    Unknown = 0,
    PciE = 1,
    NvLink = 2,
    NvSwitchPort = 3,
    AmdInfinityFabric = 4,
    CxlMem = 5,
    CxlCache = 6,
    QpiUpi = 7,
    Cxio = 8,
    Ethernet = 16,
    Infiniband = 17,
    RoceV2 = 18,
    Loopback = 19,
};

inline constexpr std::size_t link_kind_count = 13;

[[nodiscard]] constexpr std::string_view link_kind_name(LinkKind K) noexcept {
    switch (K) {
        case LinkKind::Unknown:
            return "Unknown";
        case LinkKind::PciE:
            return "PciE";
        case LinkKind::NvLink:
            return "NvLink";
        case LinkKind::NvSwitchPort:
            return "NvSwitchPort";
        case LinkKind::AmdInfinityFabric:
            return "AmdInfinityFabric";
        case LinkKind::CxlMem:
            return "CxlMem";
        case LinkKind::CxlCache:
            return "CxlCache";
        case LinkKind::QpiUpi:
            return "QpiUpi";
        case LinkKind::Cxio:
            return "Cxio";
        case LinkKind::Ethernet:
            return "Ethernet";
        case LinkKind::Infiniband:
            return "Infiniband";
        case LinkKind::RoceV2:
            return "RoceV2";
        case LinkKind::Loopback:
            return "Loopback";
        default:
            return std::string_view{"<unknown LinkKind>"};
    }
}

// The underlying values are the OSI layer numbers, so a cast to an integer
// yields the layer number itself.
enum class LinkLayer : std::uint8_t {
    Unknown = 0,
    L2 = 2,
    L3 = 3,
};

inline constexpr std::size_t link_layer_count = 3;

[[nodiscard]] constexpr std::string_view link_layer_name(LinkLayer L) noexcept {
    switch (L) {
        case LinkLayer::Unknown:
            return "Unknown";
        case LinkLayer::L2:
            return "L2";
        case LinkLayer::L3:
            return "L3";
        default:
            return std::string_view{"<unknown LinkLayer>"};
    }
}

[[nodiscard]] constexpr LinkLayer link_layer_for(LinkKind K) noexcept {
    auto raw = static_cast<std::uint8_t>(K);
    if (raw == 0) return LinkLayer::Unknown;
    if (raw >= 1 && raw <= 15) return LinkLayer::L2;
    if (raw >= 16 && raw <= 31) return LinkLayer::L3;
    return LinkLayer::Unknown;
}

enum class CongestionState : std::uint8_t {
    Healthy = 0,
    Mild = 1,
    Severe = 2,
    Saturated = 3,
    Down = 4,
};

inline constexpr std::size_t congestion_state_count = 5;

[[nodiscard]] constexpr std::string_view congestion_state_name(CongestionState C) noexcept {
    switch (C) {
        case CongestionState::Healthy:
            return "Healthy";
        case CongestionState::Mild:
            return "Mild";
        case CongestionState::Severe:
            return "Severe";
        case CongestionState::Saturated:
            return "Saturated";
        case CongestionState::Down:
            return "Down";
        default:
            return std::string_view{"<unknown CongestionState>"};
    }
}

// One entry holds a single directed half-edge.  The two directions of a link
// are separate entries so that each direction carries its own measurement,
// because a link is frequently asymmetric.  Pairing the two halves happens
// outside this header.  A null peer marks an edge whose peer reachability is
// not validated yet.
struct TopologyEdge {
    EdgeId id{};
    LinkKind kind{LinkKind::Unknown};
    CongestionState state{CongestionState::Healthy};
    std::uint8_t pad1[2]{};
    cog::CogIdentity const* peer{nullptr};
    safety::Tagged<std::uint64_t, safety::source::Calibrated> bandwidth_bytes_per_sec{0};
    safety::Tagged<std::uint64_t, safety::source::Calibrated> rtt_ns_p50{0};
    safety::Tagged<std::uint64_t, safety::source::Calibrated> rtt_ns_p99{0};
    safety::Tagged<float, safety::source::Calibrated> drop_rate{0.0f};
    std::uint8_t pad2[20]{};
};

static_assert(sizeof(TopologyEdge) == 64, "TopologyEdge must be exactly one cache line — adjust pad2 if a "
                                          "field's storage class changes.");
static_assert(alignof(TopologyEdge) == 8);
static_assert(std::is_trivially_destructible_v<TopologyEdge>,
              "TopologyEdge must be trivially destructible — passive POD.");
static_assert(std::is_trivially_copyable_v<TopologyEdge>,
              "TopologyEdge must be trivially copyable — value semantics let a "
              "builder fill arena-backed buffers without a per-edge constructor "
              "call.");
static_assert(std::is_standard_layout_v<TopologyEdge>, "TopologyEdge must be standard-layout for serialization.");

template <class Ctx>
concept CtxFitsTopologyGraph = effects::IsExecCtx<Ctx> && effects::CtxOwnsCapability<Ctx, effects::Effect::Init>;

// The spans view storage that is owned outside this class.  Copy and move are
// deleted because the graph is the one canonical fleet topology.  A copy would
// hide staleness and a move would dangle the references already handed out to
// readers.
class TopologyGraph {
public:
    constexpr TopologyGraph(TopologyGraph const&) = delete;
    constexpr TopologyGraph(TopologyGraph&&) = delete;
    TopologyGraph& operator=(TopologyGraph const&) = delete;
    TopologyGraph& operator=(TopologyGraph&&) = delete;
    ~TopologyGraph() = default;

    [[nodiscard]] constexpr std::span<const cog::CogIdentity> nodes() const noexcept { return nodes_; }

    [[nodiscard]] constexpr std::span<const TopologyEdge> edges() const noexcept { return edges_; }

    [[nodiscard]] constexpr std::size_t node_count() const noexcept { return nodes_.size(); }

    [[nodiscard]] constexpr std::size_t edge_count() const noexcept { return edges_.size(); }

    [[nodiscard]] constexpr TopologyEdge const* edge_by_id(EdgeId id) const noexcept pre(!id.is_none()) {
        for (auto const& e : edges_) {
            if (e.id == id) return &e;
        }
        return nullptr;
    }

    // The return is the number of incident edges found, which can exceed the
    // number of pointers written.  Anything past the end of out is dropped.
    [[nodiscard]] constexpr std::size_t edges_incident_on(cog::CogIdentity const* node,
                                                          std::span<TopologyEdge const*> out) const noexcept
        pre(node != nullptr) {
        std::size_t found = 0;
        std::size_t written = 0;
        for (auto const& e : edges_) {
            if (e.peer == node) {
                if (written < out.size()) {
                    out[written] = &e;
                    ++written;
                }
                ++found;
            }
        }
        return found;
    }

private:
    template <effects::IsExecCtx Ctx>
        requires CtxFitsTopologyGraph<Ctx>
    friend constexpr TopologyGraph mint_topology_graph(Ctx const&, std::span<const cog::CogIdentity>,
                                                       std::span<const TopologyEdge>) noexcept;

    constexpr TopologyGraph(std::span<const cog::CogIdentity> nodes, std::span<const TopologyEdge> edges) noexcept
        : nodes_{nodes}, edges_{edges} {}

    std::span<const cog::CogIdentity> nodes_{};
    std::span<const TopologyEdge> edges_{};
};

// The mint does not verify that every peer points into the node set, nor that
// edge ids are unique.  Both sweeps are quadratic in the size of the graph, so
// the caller owns those invariants.
//
// The return is by value even though copy and move are deleted.  Guaranteed
// copy elision constructs the prvalue directly at the destination, so no copy
// takes place.  The deleted operations forbid only a later copy.
template <effects::IsExecCtx Ctx>
    requires CtxFitsTopologyGraph<Ctx>
[[nodiscard]] constexpr TopologyGraph mint_topology_graph(Ctx const& /* ctx */, std::span<const cog::CogIdentity> nodes,
                                                          std::span<const TopologyEdge> edges) noexcept {
    return TopologyGraph{nodes, edges};
}

}  // namespace crucible::topology

namespace crucible::topology::detail::topology_graph_self_test {

[[nodiscard]] consteval bool every_link_kind_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^LinkKind));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (link_kind_name([:en:]) == std::string_view{"<unknown LinkKind>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_link_kind_has_name(), "link_kind_name() switch is missing an arm for at least one "
                                          "LinkKind atom.");

[[nodiscard]] consteval bool every_link_layer_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^LinkLayer));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (link_layer_name([:en:]) == std::string_view{"<unknown LinkLayer>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_link_layer_has_name());

[[nodiscard]] consteval bool every_congestion_state_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^CongestionState));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (congestion_state_name([:en:]) == std::string_view{"<unknown CongestionState>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_congestion_state_has_name());

static_assert(link_layer_for(LinkKind::Unknown) == LinkLayer::Unknown);
static_assert(link_layer_for(LinkKind::PciE) == LinkLayer::L2);
static_assert(link_layer_for(LinkKind::NvLink) == LinkLayer::L2);
static_assert(link_layer_for(LinkKind::NvSwitchPort) == LinkLayer::L2);
static_assert(link_layer_for(LinkKind::AmdInfinityFabric) == LinkLayer::L2);
static_assert(link_layer_for(LinkKind::CxlMem) == LinkLayer::L2);
static_assert(link_layer_for(LinkKind::CxlCache) == LinkLayer::L2);
static_assert(link_layer_for(LinkKind::QpiUpi) == LinkLayer::L2);
static_assert(link_layer_for(LinkKind::Cxio) == LinkLayer::L2);
static_assert(link_layer_for(LinkKind::Ethernet) == LinkLayer::L3);
static_assert(link_layer_for(LinkKind::Infiniband) == LinkLayer::L3);
static_assert(link_layer_for(LinkKind::RoceV2) == LinkLayer::L3);
static_assert(link_layer_for(LinkKind::Loopback) == LinkLayer::L3);

static_assert(EdgeId{}.is_none());
static_assert(!EdgeId{0}.is_none(), "EdgeId{0} must be a real ID, "
                                    "not the sentinel — UINT32_MAX is the sentinel.");
static_assert(EdgeId::none().raw() == UINT32_MAX);
static_assert(EdgeId{42}.raw() == 42);
static_assert(EdgeId{42} == EdgeId{42});
static_assert(EdgeId{1} < EdgeId{2});

static_assert(
    [] {
        TopologyEdge e{};
        return e.id.is_none() && e.kind == LinkKind::Unknown && e.state == CongestionState::Healthy && e.peer == nullptr
            && e.bandwidth_bytes_per_sec.value() == 0 && e.rtt_ns_p50.value() == 0
            && e.rtt_ns_p99.value() == 0
            // Compare the bits, because a float equality test is a hard error
            // under the project warning set.
            && std::bit_cast<std::uint32_t>(e.drop_rate.value()) == 0u;
    }(),
    "Default TopologyEdge state drifted from the zero specification.");

static_assert(
    [] {
        using InitCtx = effects::ExecCtx<effects::Init, effects::ctx_numa::Any, effects::ctx_alloc::Unbound,
                                         effects::ctx_heat::Cold, effects::ctx_resid::DRAM,
                                         effects::Row<effects::Effect::Init>, effects::ctx_workload::Unspecified>;
        InitCtx ctx{};
        auto g = mint_topology_graph(ctx, std::span<const cog::CogIdentity>{}, std::span<const TopologyEdge>{});
        return g.node_count() == 0 && g.edge_count() == 0;
    }(),
    "Default-minted empty TopologyGraph reports non-zero counts — "
    "span size projection broken.");

using InitCtx =
    effects::ExecCtx<effects::Init, effects::ctx_numa::Any, effects::ctx_alloc::Unbound, effects::ctx_heat::Cold,
                     effects::ctx_resid::DRAM, effects::Row<effects::Effect::Init>, effects::ctx_workload::Unspecified>;
static_assert(CtxFitsTopologyGraph<InitCtx>);

// A background context is refused.  The graph is built once during
// initialisation, so a later mint would either race the builder or rebuild the
// graph while traffic reads it.
using BgCtx = effects::ExecCtx<effects::Bg, effects::ctx_numa::Any, effects::ctx_alloc::Arena, effects::ctx_heat::Warm,
                               effects::ctx_resid::L3, effects::Row<effects::Effect::Bg, effects::Effect::Alloc>,
                               effects::ctx_workload::Unspecified>;
static_assert(!CtxFitsTopologyGraph<BgCtx>);

using TestCtx =
    effects::ExecCtx<effects::Test, effects::ctx_numa::Any, effects::ctx_alloc::Unbound, effects::ctx_heat::Cold,
                     effects::ctx_resid::DRAM, effects::Row<effects::Effect::Test>, effects::ctx_workload::Unspecified>;
static_assert(!CtxFitsTopologyGraph<TestCtx>);

static_assert(!CtxFitsTopologyGraph<int>);

// Renumbering any of the values pinned below re-keys every persisted snapshot
// that mentions the affected value.  A new atom takes the next free underlying
// value in its range and leaves the existing pins alone.
static_assert(static_cast<std::uint8_t>(LinkKind::Unknown) == 0,
              "LinkKind::Unknown drifted — persisted ordinals invalidated.");
static_assert(static_cast<std::uint8_t>(LinkKind::PciE) == 1);
static_assert(static_cast<std::uint8_t>(LinkKind::NvLink) == 2);
static_assert(static_cast<std::uint8_t>(LinkKind::NvSwitchPort) == 3);
static_assert(static_cast<std::uint8_t>(LinkKind::AmdInfinityFabric) == 4);
static_assert(static_cast<std::uint8_t>(LinkKind::CxlMem) == 5);
static_assert(static_cast<std::uint8_t>(LinkKind::CxlCache) == 6);
static_assert(static_cast<std::uint8_t>(LinkKind::QpiUpi) == 7);
static_assert(static_cast<std::uint8_t>(LinkKind::Cxio) == 8);
static_assert(static_cast<std::uint8_t>(LinkKind::Ethernet) == 16);
static_assert(static_cast<std::uint8_t>(LinkKind::Infiniband) == 17);
static_assert(static_cast<std::uint8_t>(LinkKind::RoceV2) == 18);
static_assert(static_cast<std::uint8_t>(LinkKind::Loopback) == 19);

static_assert(std::is_same_v<std::underlying_type_t<LinkKind>, std::uint8_t>,
              "LinkKind underlying type drifted from uint8_t — ABI change.");

static_assert(static_cast<std::uint8_t>(LinkLayer::Unknown) == 0);
static_assert(static_cast<std::uint8_t>(LinkLayer::L2) == 2);
static_assert(static_cast<std::uint8_t>(LinkLayer::L3) == 3);

static_assert(std::is_same_v<std::underlying_type_t<LinkLayer>, std::uint8_t>);

static_assert(static_cast<std::uint8_t>(CongestionState::Healthy) == 0);
static_assert(static_cast<std::uint8_t>(CongestionState::Mild) == 1);
static_assert(static_cast<std::uint8_t>(CongestionState::Severe) == 2);
static_assert(static_cast<std::uint8_t>(CongestionState::Saturated) == 3);
static_assert(static_cast<std::uint8_t>(CongestionState::Down) == 4);

static_assert(std::is_same_v<std::underlying_type_t<CongestionState>, std::uint8_t>);

[[nodiscard]] consteval bool link_kind_partition_sound() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^LinkKind));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        constexpr auto k = [:en:];
        constexpr auto raw = static_cast<std::uint8_t>(k);
        constexpr auto layer = link_layer_for(k);
        if (raw == 0) {
            if (layer != LinkLayer::Unknown) return false;
        } else if (raw >= 1 && raw <= 15) {
            if (layer != LinkLayer::L2) return false;
        } else if (raw >= 16 && raw <= 31) {
            if (layer != LinkLayer::L3) return false;
        } else {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(link_kind_partition_sound(), "A LinkKind atom strays outside the {0=Unknown, 1..15=L2, 16..31=L3} "
                                           "partition — link_layer_for projection silently miscategorises.  "
                                           "Adjust either the atom's underlying value or the partition.");

}  // namespace crucible::topology::detail::topology_graph_self_test
