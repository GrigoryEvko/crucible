// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The topology graph is built once, at initialisation, and every later
// reader holds it as `TopologyGraph const&`.  A test context carries no
// Init row, so it cannot mint the graph: a mock that built one for a
// contrived scenario would otherwise produce the fleet's canonical
// topology from a test thread.

#include <crucible/topology/TopologyGraph.h>

namespace topology = crucible::topology;
namespace cog = crucible::cog;

inline constexpr ::fixy::View<cog::CogIdentity const> kNoNodes{};
inline constexpr ::fixy::View<topology::TopologyEdge const> kNoEdges{};

[[maybe_unused]] static auto try_mint_with_test_ctx() {
    ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    return topology::mint_topology_graph(ctx, kNoNodes, kNoEdges);
}

int main() { return 0; }
