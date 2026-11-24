#pragma once

#include <crucible/cog/CogIdentity.h>
#include <crucible/cog/TargetCaps.h>
#include <crucible/topology/TopologyEdge.h>
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

// The ids, the kinds of a link and TopologyEdge are in
// crucible/topology/TopologyEdge.h.

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
    template <class Brand, std::size_t Extent>
    [[nodiscard]] constexpr std::size_t edges_incident_on(NodeId node,
                                                          ::fixy::ViewMut<EdgeId, Brand, Extent> out) const noexcept {
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
