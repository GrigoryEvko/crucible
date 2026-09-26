// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Minting the topology graph needs an execution context.  An `int` in the
// context slot, a placeholder left behind by a refactor or a count typed
// where the context belongs, is refused at the requires-clause, not deep
// inside the factory.

#include <crucible/topology/TopologyGraph.h>

#include <span>

namespace topology = crucible::topology;
namespace cog = crucible::cog;

inline constexpr std::span<const cog::CogIdentity> kNoNodes{};
inline constexpr std::span<const topology::TopologyEdge> kNoEdges{};

[[maybe_unused]] static auto try_mint_with_int_ctx() {
    return topology::mint_topology_graph(int{0}, kNoNodes, kNoEdges);
}

int main() { return 0; }
