// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-248 HS14 fixture — the singleton-init acyclicity proof is load-
// bearing.  This fixture builds a singleton init-dependency graph that
// CONTAINS a cycle (singleton 0's lazy init touches 1, and 1's touches 0)
// and feeds it to the SAME S004 detector the production registry
// (safety/SingletonInitGraph.h) uses.  The acyclicity `static_assert`
// MUST fail to compile — proving that a real static-initialization-order
// fiasco among Meyers singletons would redden the build, not slip through.
//
// Mismatch class: detected init cycle (the Scenario D hazard).
//
// Expected diagnostic: the static_assert message (cycle / fiasco / S004).

#include <crucible/safety/CollisionCatalog.h>   // pack::singleton_init_acyclic

#include <array>
#include <cstddef>
#include <utility>

namespace pak = crucible::safety::fn::collision::pack;

// Two mutually-init-dependent singletons: 0 -> 1 and 1 -> 0.  A cycle.
inline constexpr std::array<std::pair<std::size_t, std::size_t>, 2> kCyclicGraph{{{0, 1}, {1, 0}}};

static_assert(
    pak::singleton_init_acyclic<2>(kCyclicGraph),
    "S004: Meyers-singleton lazy-init graph has a cycle — "
    "static-initialization-order fiasco.");

int main() { return 0; }
