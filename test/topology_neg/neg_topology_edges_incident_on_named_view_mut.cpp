// Negative-compile test.  This file must fail to compile.
//
// edges_incident_on writes the ids of the edges into the run of a frame, and
// it takes the ViewMut of the frame by value.  A ViewMut is the one writer of
// its run and does not copy.  So a call with the named ViewMut of the body
// does not compile: the caller passes std::move(run), and the body then
// holds no second writer of the run.

#include <crucible/topology/TopologyGraph.h>

namespace topology = crucible::topology;
namespace cog = crucible::cog;

int main() {
    ::fixy::ColdInitCtx ctx{::foundation::effects::testing::init()};
    auto const graph = topology::mint_topology_graph(ctx, ::fixy::View<cog::CogIdentity const>{},
                                                     ::fixy::View<topology::TopologyEdge const>{});
    ::fixy::FixedArray<topology::EdgeId, 4> incident{};
    return incident.with_mut([&graph](auto run) noexcept -> int {
        return static_cast<int>(graph.edges_incident_on(topology::NodeId{0}, run));
    });
}
