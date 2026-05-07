// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 2 for topology::TopologyGraph (GAPS-110 #1209).
//
// Premise: CtxFitsTopologyGraph<Ctx> conjuncts on
//
//   (1) IsExecCtx<Ctx>                       — must be a typed
//                                              ExecCtx, not a bare
//                                              integral / pointer
//                                              type that happens to
//                                              compile.
//
// — minting a TopologyGraph requires an Init-row context proof.  A
// bare `int` (or `void*`, or any non-ExecCtx type) is the canonical
// shape of a placeholder argument that slipped through during a
// refactor (e.g., `auto ctx = 0;` left over from a debug stub) or
// during typo-driven argument reordering (someone typed a count
// where ctx should go).  The IsExecCtx conjunct refuses substitution
// at the call site so the bug surfaces here, not 50 lines downstream
// where the resulting empty TopologyGraph silently misroutes traffic.
//
// Without this gate, mint_topology_graph(0, ...) would either:
//
//   (a) deduce Ctx = int and produce a constraint violation at the
//       BODY of the factory when it tried to read row_type_of_t<int>,
//       producing a deep-template error 30 lines into the diagnostic.
//   (b) construct a TopologyGraph from arbitrary (nodes, edges)
//       spans that some caller was about to pass to a typed-context
//       overload.  Federation cache keys would be computed with a
//       garbage row tag.
//
// The IsExecCtx conjunct catches the misuse at requires-clause
// substitution time, naming `CtxFitsTopologyGraph<int>` in the
// diagnostic.

#include <crucible/topology/TopologyGraph.h>

#include <span>

namespace topology = crucible::topology;
namespace cog      = crucible::cog;

// Construct empty spans so the call shape compiles up to the point
// where the requires-clause substitutes `int` for Ctx.
inline constexpr std::span<const cog::CogIdentity> kNoNodes{};
inline constexpr std::span<const topology::TopologyEdge> kNoEdges{};

// The decisive call.  `int{0}` is NOT an IsExecCtx; the requires-clause
// refuses substitution and the build fails here.
[[maybe_unused]] static auto try_mint_with_int_ctx() {
    return topology::mint_topology_graph(int{0}, kNoNodes, kNoEdges);
}

int main() { return 0; }
