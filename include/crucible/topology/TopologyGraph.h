#pragma once

#include <crucible/cog/CogIdentity.h>
#include <crucible/cog/TargetCaps.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Pre.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Row.h>

#include <compare>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <type_traits>

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

// The position of a node in the node table of a graph.
struct NodeId {
    std::uint32_t value_ = UINT32_MAX;

    constexpr NodeId() noexcept = default;
    explicit constexpr NodeId(std::uint32_t v) noexcept : value_{v} {}

    [[nodiscard]] constexpr std::uint32_t raw() const noexcept { return value_; }
    [[nodiscard]] constexpr bool is_none() const noexcept { return value_ == UINT32_MAX; }
    [[nodiscard]] static constexpr NodeId none() noexcept { return NodeId{UINT32_MAX}; }

    constexpr auto operator<=>(NodeId const&) const = default;
};

// foundation::reflect::enum_name gives the log spelling of the three enums
// below, and foundation::reflect::enum_count their cardinality.

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

// The underlying values are the OSI layer numbers, so a cast to an integer
// yields the layer number itself.
enum class LinkLayer : std::uint8_t {
    Unknown = 0,
    L2 = 2,
    L3 = 3,
};

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

// One entry holds a single directed half-edge.  The two directions of a link
// are separate entries so that each direction carries its own measurement,
// because a link is frequently asymmetric.  Pairing the two halves happens
// outside this header.  The peer is the position of the far node in the node
// table of the graph.  A peer of none marks an edge whose peer reachability
// is not validated yet.
struct TopologyEdge {
    EdgeId id{};
    LinkKind kind{LinkKind::Unknown};
    CongestionState state{CongestionState::Healthy};
    ::foundation::core::FixedArray<std::uint8_t, 2> pad1{};
    NodeId peer{};
    ::foundation::core::FixedArray<std::uint8_t, 4> pad_after_peer{};
    // A measured quantity.  Calibrated is a source tag, so each field stays
    // trivially copyable and a default edge holds a zero measurement.
    cog::CalibratedValue<std::uint64_t> bandwidth_bytes_per_sec{};
    cog::CalibratedValue<std::uint64_t> rtt_ns_p50{};
    cog::CalibratedValue<std::uint64_t> rtt_ns_p99{};
    cog::CalibratedValue<float> drop_rate{};
    ::foundation::core::FixedArray<std::uint8_t, 20> pad2{};
};

template <class Ctx>
concept CtxFitsTopologyGraph =
    ::foundation::effects::IsExecCtx<Ctx>
    && ::foundation::effects::CtxAdmits<Ctx, ::foundation::effects::Row<::foundation::effects::Effect::Init>>;

// The views borrow storage that is owned outside this class.  Copy and move
// are deleted because the graph is the one canonical fleet topology.  A copy
// would hide staleness and a move would dangle the references already handed
// out to readers.
class TopologyGraph {
public:
    constexpr TopologyGraph(TopologyGraph const&) = delete;
    constexpr TopologyGraph(TopologyGraph&&) = delete;
    TopologyGraph& operator=(TopologyGraph const&) = delete;
    TopologyGraph& operator=(TopologyGraph&&) = delete;
    ~TopologyGraph() = default;

    [[nodiscard]] constexpr ::fixy::View<cog::CogIdentity const> nodes() const noexcept { return nodes_; }

    [[nodiscard]] constexpr ::fixy::View<TopologyEdge const> edges() const noexcept { return edges_; }

    [[nodiscard]] constexpr std::size_t node_count() const noexcept { return nodes_.size(); }

    [[nodiscard]] constexpr std::size_t edge_count() const noexcept { return edges_.size(); }

    // A copy of the edge with the id, or no value.  Complexity: linear in
    // the edges.
    [[nodiscard]] constexpr ::fixy::Option<TopologyEdge> edge_by_id(EdgeId id) const noexcept {
        CRUCIBLE_PRE(!id.is_none());
        for (TopologyEdge const& edge : edges_) {
            if (edge.id == id) return ::fixy::some(edge);
        }
        return ::fixy::none;
    }

    // Writes the id of each edge whose peer is node into out, in the order
    // of the edges.  The return is the number of incident edges found, which
    // can exceed the number of ids written.  Each id past the end of out is
    // dropped.  Complexity: linear in the edges.
    [[nodiscard]] constexpr std::size_t edges_incident_on(NodeId node, ::fixy::View<EdgeId> out) const noexcept {
        CRUCIBLE_PRE(!node.is_none());
        std::size_t found = 0;
        auto cursor = out.begin();
        auto const end = out.end();
        for (TopologyEdge const& edge : edges_) {
            if (edge.peer == node) {
                if (cursor != end) {
                    *cursor = edge.id;
                    ++cursor;
                }
                ++found;
            }
        }
        return found;
    }

private:
    template <::foundation::effects::IsExecCtx Ctx>
        requires CtxFitsTopologyGraph<Ctx>
    friend constexpr TopologyGraph mint_topology_graph(Ctx const&, ::fixy::View<cog::CogIdentity const>,
                                                       ::fixy::View<TopologyEdge const>) noexcept;

    constexpr TopologyGraph(::fixy::View<cog::CogIdentity const> nodes, ::fixy::View<TopologyEdge const> edges) noexcept
        : nodes_{nodes}, edges_{edges} {}

    ::fixy::View<cog::CogIdentity const> nodes_{};
    ::fixy::View<TopologyEdge const> edges_{};
};

// The mint does not verify that every peer points into the node set, nor that
// edge ids are unique.  Both sweeps are quadratic in the size of the graph, so
// the caller owns those invariants.
//
// The return is by value even though copy and move are deleted.  Guaranteed
// copy elision constructs the prvalue directly at the destination, so no copy
// takes place.  The deleted operations forbid only a later copy.
template <::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsTopologyGraph<Ctx>
[[nodiscard]] constexpr TopologyGraph mint_topology_graph(Ctx const& /* ctx */,
                                                          ::fixy::View<cog::CogIdentity const> nodes,
                                                          ::fixy::View<TopologyEdge const> edges) noexcept {
    return TopologyGraph{nodes, edges};
}

}  // namespace crucible::topology
