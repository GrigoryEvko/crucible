// FIXY-V-248 — safety/SingletonInitGraph.h positive sentinel (Scenario D).
//
// Forces the registry header's in-file proof + self-tests to be checked
// under the project warning flags, and adds TU-level cross-cutting checks:
// every registered singleton routes to GlobalState, tags are distinct, the
// production graph is acyclic, and the S004 detector reports a synthetic
// cycle as cyclic (so the acyclicity proof is load-bearing, not vacuous).

#include <crucible/safety/SingletonInitGraph.h>

#include <array>
#include <type_traits>
#include <utility>

namespace sig = crucible::safety::singleton_init_graph;
namespace pak = crucible::safety::fn::collision::pack;
namespace fg  = crucible::fixy::grant;
using D       = crucible::fixy::dim::DimensionAxis;

namespace {

// (1) Each production Meyers singleton is registered as a GlobalState grant.
static_assert(fg::IsGrantTag<sig::CKernelTableSingleton>);
static_assert(fg::IsGrantTag<sig::HotRegionRegistrySingleton>);
static_assert(fg::IsGrantTag<sig::BpfLogCbSingleton>);
static_assert(fg::which_dim_v<sig::CKernelTableSingleton>      == D::GlobalState);
static_assert(fg::which_dim_v<sig::HotRegionRegistrySingleton> == D::GlobalState);
static_assert(fg::which_dim_v<sig::BpfLogCbSingleton>          == D::GlobalState);

// (2) Three distinct tags → three distinct singleton grant types.
static_assert(!std::is_same_v<sig::CKernelTableSingleton, sig::HotRegionRegistrySingleton>);
static_assert(!std::is_same_v<sig::HotRegionRegistrySingleton, sig::BpfLogCbSingleton>);
static_assert(sig::kSingletonCount == 3);

// (3) The production singleton lazy-init graph is acyclic.
static_assert(pak::singleton_init_acyclic<sig::kSingletonCount>(sig::kInitEdges));
static_assert(!pak::singleton_init_has_cycle<sig::kSingletonCount>(sig::kInitEdges));

// (4) The detector is load-bearing: a self-loop and a 3-cycle are reported
//     as cyclic; a linear chain over the same node count is acyclic.
inline constexpr std::array<std::pair<std::size_t, std::size_t>, 1> kSelfLoop{{{0, 0}}};
inline constexpr std::array<std::pair<std::size_t, std::size_t>, 3> kThreeCycle{{{0, 1}, {1, 2}, {2, 0}}};
inline constexpr std::array<std::pair<std::size_t, std::size_t>, 2> kLinearChain{{{0, 1}, {1, 2}}};
static_assert(pak::singleton_init_has_cycle<3>(kSelfLoop));
static_assert(pak::singleton_init_has_cycle<3>(kThreeCycle));
static_assert(pak::singleton_init_acyclic<3>(kLinearChain));

}  // namespace

int main() { return 0; }
