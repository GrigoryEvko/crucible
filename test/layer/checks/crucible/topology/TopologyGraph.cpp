// The compile-time checks of crucible/topology/TopologyGraph.h.

#include <crucible/topology/TopologyGraph.h>

namespace crucible::topology {

static_assert(sizeof(EdgeId) == sizeof(std::uint32_t),
              "EdgeId must collapse to a bare uint32_t at runtime — strong ID is "
              "phantom-typed at compile time only.");
static_assert(std::is_trivially_destructible_v<EdgeId>);
static_assert(std::is_trivially_copyable_v<EdgeId>);

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

}  // namespace crucible::topology

namespace crucible::topology::detail::topology_graph_self_test {

static_assert(EdgeId{}.is_none());
static_assert(!EdgeId{0}.is_none(), "EdgeId{0} must be a real ID, "
                                    "not the sentinel — UINT32_MAX is the sentinel.");
static_assert(EdgeId::none().raw() == UINT32_MAX);
static_assert(EdgeId{42}.raw() == 42);
static_assert(EdgeId{42} == EdgeId{42});
static_assert(EdgeId{1} < EdgeId{2});

// The graph is built once during initialisation.  A background context
// would either race the builder or rebuild the graph while traffic reads
// it, and a test context carries no Init row.
static_assert(CtxFitsTopologyGraph<::fixy::ColdInitCtx>);
static_assert(!CtxFitsTopologyGraph<::fixy::BgDrainCtx>);
static_assert(!CtxFitsTopologyGraph<::fixy::TestRunnerCtx>);
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

// Every kind lands in the layer its value range names.  The walk reads the
// enumerators by reflection, so a kind added outside every range fails here.
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
