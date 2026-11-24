#pragma once

// The ids, the kinds of a link and the edge of the topology graph.  A
// CogIdentity holds its neighbor edges as Views of TopologyEdge, so
// crucible/cog/CogIdentity.h includes this header, and the edge is complete
// in each unit that holds a CogIdentity.  crucible/topology/TopologyGraph.h
// holds the graph of the nodes and the edges.

#include <crucible/cog/Calibrated.h>
#include <fixy/Core.h>

#include <compare>
#include <cstdint>

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

// foundation::core::enum_name_text gives the log spelling of the three enums
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
    ::fixy::FixedArray<std::uint8_t, 2> pad1{};
    NodeId peer{};
    ::fixy::FixedArray<std::uint8_t, 4> pad_after_peer{};
    // A measured quantity.  Calibrated is a source tag, so each field stays
    // trivially copyable and a default edge holds a zero measurement.
    cog::CalibratedValue<std::uint64_t> bandwidth_bytes_per_sec{};
    cog::CalibratedValue<std::uint64_t> rtt_ns_p50{};
    cog::CalibratedValue<std::uint64_t> rtt_ns_p99{};
    cog::CalibratedValue<float> drop_rate{};
    ::fixy::FixedArray<std::uint8_t, 20> pad2{};
};

}  // namespace crucible::topology
