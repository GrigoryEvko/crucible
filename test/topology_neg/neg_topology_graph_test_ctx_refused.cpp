// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The topology graph is built once, at initialisation, and every later
// reader holds it as `TopologyGraph const&`.  A test context carries no
// Init row, so it cannot mint the graph: a mock that built one for a
// contrived scenario would otherwise produce the fleet's canonical
// topology from a test thread.

#include <crucible/topology/TopologyGraph.h>

#include <span>

namespace topology = crucible::topology;
namespace cog = crucible::cog;

inline constexpr std::span<const cog::CogIdentity> kNoNodes{};
inline constexpr std::span<const topology::TopologyEdge> kNoEdges{};

[[maybe_unused]] static auto try_mint_with_test_ctx() {
    ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    return topology::mint_topology_graph(ctx, kNoNodes, kNoEdges);
}

int main() { return 0; }
