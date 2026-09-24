#pragma once

// Every function-local static singleton in the codebase is registered here,
// and the build proves their lazy-initialization graph is acyclic.  A cycle is
// the lazy form of the static-initialization-order problem: the first thread to
// touch either of two mutually dependent singletons re-enters initialization and
// reads a half-constructed peer.
//
// Register each singleton's tag here when you add one, along with any edge its
// initializer creates.  An edge (from, to) reads "the initializer of `from`
// touches singleton `to`".
//
// The registry is central rather than annotated at each singleton, because the
// grant and detector headers would then enter the include graph of the headers
// that hold those singletons, one of which is hot and widely included.  Here the
// dependency stays in one cold header and the whole graph is visible at once.

#include <crucible/fixy/grant/_Global.h>
#include <crucible/safety/CollisionCatalog.h>

#include <array>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace crucible::safety::singleton_init_graph {

namespace gg = ::crucible::fixy::grant::global;
namespace pak = ::crucible::safety::fn::collision::pack;

struct CKernelTableTag final {};  // global_ckernel_table()
struct HotRegionRegistryTag final {};  // HotRegionRegistry::instance()
struct BpfLogCbTag final {};  // install_libbpf_log_cb_once()

using CKernelTableSingleton = gg::singleton<CKernelTableTag>;
using HotRegionRegistrySingleton = gg::singleton<HotRegionRegistryTag>;
using BpfLogCbSingleton = gg::singleton<BpfLogCbTag>;

inline constexpr std::size_t kCKernelTable = 0;
inline constexpr std::size_t kHotRegionRegistry = 1;
inline constexpr std::size_t kBpfLogCb = 2;
inline constexpr std::size_t kSingletonCount = 3;

// Empty because no registered initializer reaches another singleton.
inline constexpr std::array<std::pair<std::size_t, std::size_t>, 0> kInitEdges{};

static_assert(pak::singleton_init_acyclic<kSingletonCount>(kInitEdges),
              "Crucible Meyers-singleton lazy-init graph has a cycle — "
              "static-initialization-order fiasco (S004). Break the cycle or merge "
              "the participating singletons into one initialization unit.");

namespace detail::self_test {

namespace fg = ::crucible::fixy::grant;
using D = ::crucible::fixy::dim::DimensionAxis;

static_assert(fg::IsGrantTag<CKernelTableSingleton>);
static_assert(fg::IsGrantTag<HotRegionRegistrySingleton>);
static_assert(fg::IsGrantTag<BpfLogCbSingleton>);
static_assert(fg::which_dim_v<CKernelTableSingleton> == D::GlobalState);
static_assert(fg::which_dim_v<HotRegionRegistrySingleton> == D::GlobalState);
static_assert(fg::which_dim_v<BpfLogCbSingleton> == D::GlobalState);

static_assert(!std::is_same_v<CKernelTableSingleton, HotRegionRegistrySingleton>);
static_assert(!std::is_same_v<HotRegionRegistrySingleton, BpfLogCbSingleton>);
static_assert(!std::is_same_v<CKernelTableSingleton, BpfLogCbSingleton>);

// The detector reports a synthetic two-cycle, which is what shows the proof
// above would fire on a real cycle rather than holding vacuously.
inline constexpr std::array<std::pair<std::size_t, std::size_t>, 2> kCyclicProbe{{{0, 1}, {1, 0}}};
static_assert(pak::singleton_init_has_cycle<2>(kCyclicProbe));
static_assert(pak::singleton_init_acyclic<kSingletonCount>(kInitEdges));

}  // namespace detail::self_test

}  // namespace crucible::safety::singleton_init_graph
