// Sentinel TU: compiles the registry header under the project warning flags so
// its in-file proof and self-tests run.

#include <crucible/safety/SingletonInitGraph.h>

#include <array>
#include <type_traits>
#include <utility>

namespace sig = crucible::safety::singleton_init_graph;
namespace pak = crucible::safety::fn::collision::pack;
namespace fg = crucible::fixy::grant;
using D = crucible::fixy::dim::DimensionAxis;

namespace {

static_assert(fg::IsGrantTag<sig::CKernelTableSingleton>);
static_assert(fg::IsGrantTag<sig::HotRegionRegistrySingleton>);
static_assert(fg::IsGrantTag<sig::BpfLogCbSingleton>);
static_assert(fg::which_dim_v<sig::CKernelTableSingleton> == D::GlobalState);
static_assert(fg::which_dim_v<sig::HotRegionRegistrySingleton> == D::GlobalState);
static_assert(fg::which_dim_v<sig::BpfLogCbSingleton> == D::GlobalState);

static_assert(!std::is_same_v<sig::CKernelTableSingleton, sig::HotRegionRegistrySingleton>);
static_assert(!std::is_same_v<sig::HotRegionRegistrySingleton, sig::BpfLogCbSingleton>);
static_assert(sig::kSingletonCount == 3);

static_assert(pak::singleton_init_acyclic<sig::kSingletonCount>(sig::kInitEdges));
static_assert(!pak::singleton_init_has_cycle<sig::kSingletonCount>(sig::kInitEdges));

// These synthetic graphs keep the acyclicity proof above from being vacuous.  A
// self-loop and a three-cycle must report as cyclic, and a linear chain over the
// same node count must report as acyclic.
inline constexpr std::array<std::pair<std::size_t, std::size_t>, 1> kSelfLoop{{{0, 0}}};
inline constexpr std::array<std::pair<std::size_t, std::size_t>, 3> kThreeCycle{{{0, 1}, {1, 2}, {2, 0}}};
inline constexpr std::array<std::pair<std::size_t, std::size_t>, 2> kLinearChain{{{0, 1}, {1, 2}}};
static_assert(pak::singleton_init_has_cycle<3>(kSelfLoop));
static_assert(pak::singleton_init_has_cycle<3>(kThreeCycle));
static_assert(pak::singleton_init_acyclic<3>(kLinearChain));

}  // namespace

int main() { return 0; }
