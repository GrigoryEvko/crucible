// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 2 for topology::TopologyGraph (GAPS-110 #1209).
//
// Premise: CtxFitsTopologyGraph<Ctx> conjuncts on
//
//   (2) row_contains_v<row_type_of_t<Ctx>, Effect::Init>
//
// — TopologyGraph is built ONCE at startup by Discovery (GAPS-111).
// Once minted, it is the canonical fleet topology referenced by
// Mimic / runtime observation / Canopy as `const TopologyGraph&`.  A non-Init
// context attempting to mint either:
//
//   (a) means a foreground or test-only thread is racing Discovery
//       (BorrowSafe + ThreadSafe violation — there is no synchronisation
//       protocol that admits multiple TopologyGraph minters), or
//   (b) means a bg-only context is rebuilding the graph mid-flight
//       (which we explicitly forbid; resharding goes through a fresh
//       Init context after Cipher cold-tier promotion drops the
//       previous topology arena).
//
// The fixture witnesses that a Test-row ctx (Effect::Test only, no
// Init) is refused at the row_contains conjunct.  The diagnostic
// names `CtxFitsTopologyGraph<TestCtx>` on the failed concept arm,
// pointing at the missing `Init` ingress.
//
// Without this gate, a unit-test mock attempting to construct a
// TopologyGraph for a contrived test scenario would silently mint
// the graph; the federation row_hash would then be computed with a
// Test-row tag, polluting any cross-test reuse of the cache slot.

#include <crucible/topology/TopologyGraph.h>

#include <span>

namespace topology = crucible::topology;
namespace cog      = crucible::cog;
namespace effects  = crucible::effects;

using TestCtx = effects::ExecCtx<
    effects::Test,
    effects::ctx_numa::Any,
    effects::ctx_alloc::Unbound,
    effects::ctx_heat::Cold,
    effects::ctx_resid::DRAM,
    effects::Row<effects::Effect::Test>,
    effects::ctx_workload::Unspecified>;

inline constexpr std::span<const cog::CogIdentity> kNoNodes{};
inline constexpr std::span<const topology::TopologyEdge> kNoEdges{};

// The decisive call: TestCtx satisfies IsExecCtx (so the first
// conjunct passes) but its effect row carries Effect::Test, NOT
// Effect::Init — the second conjunct refuses substitution.
[[maybe_unused]] static auto try_mint_with_test_ctx() {
    TestCtx ctx{};
    return topology::mint_topology_graph(ctx, kNoNodes, kNoEdges);
}

int main() { return 0; }
