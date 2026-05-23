#pragma once

// ── crucible::safety::singleton_init_graph — Scenario D (FIXY-V-248) ──
//
// Central registry of Crucible's Meyers (function-local static) singletons
// + a COMPILE-TIME PROOF that their lazy-initialization dependency graph is
// acyclic — i.e. there is no static-initialization-order fiasco in its
// subtlest, lazy-init form (the first thread to touch either of two
// mutually-dependent singletons triggers a re-entrant init that observes a
// half-constructed peer).
//
// Each singleton is declared here as a `grant::global::singleton<Tag>`
// (grep target: `grant::global::singleton`) carrying a unique phantom tag,
// with a comment naming its production site.  An edge (from, to) reads
// "the lazy initializer of `from` touches singleton `to`".  The proof runs
// the S004 detector (`pack::singleton_init_acyclic`, FIXY-V-243) over the
// edge set; a cycle reddens the build.
//
// ── Why a central registry (not per-site annotations) ─────────────────
//
// The three production sites live in CKernel.h (a hot, widely-included
// header), warden/Registry.h, and perf/detail/BpfLoader.h.  Annotating at
// each site would pull `fixy/grant/Global.h` + `CollisionCatalog.h` into
// those headers' transitive include graphs — ballooning compile time
// codebase-wide and risking include cycles, for zero added safety.  This
// leaf registry confines that dependency to one cold audit header + its
// sentinel, gives a single place to see the whole singleton init graph,
// and proves acyclicity once.  Adding a new Meyers singleton anywhere?
// Register its tag + any init edges here.
//
// ── The production graph ──────────────────────────────────────────────
//
// All three singleton constructors are self-contained — none reaches into
// another singleton during its lazy init — so the edge set is EMPTY and
// the graph is trivially acyclic.  The static_assert below pins that fact:
// if a future ctor introduces a cross-singleton init dependency, add the
// edge here and the detector will reject any resulting cycle.

#include <crucible/fixy/grant/Global.h>         // grant::global::singleton, which_dim
#include <crucible/safety/CollisionCatalog.h>   // pack::singleton_init_acyclic (S004)

#include <array>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace crucible::safety::singleton_init_graph {

namespace gg  = ::crucible::fixy::grant::global;
namespace pak = ::crucible::safety::fn::collision::pack;

// ── Unique phantom tags — one per production Meyers singleton ─────────
struct CKernelTableTag      final {};  // CKernel.h:603  global_ckernel_table()
struct HotRegionRegistryTag final {};  // warden/Registry.h:69  HotRegionRegistry::instance()
struct BpfLogCbTag          final {};  // perf/detail/BpfLoader.h:195  install_libbpf_log_cb_once()

// ── Each singleton declared as grant::global::singleton<Tag> ──────────
using CKernelTableSingleton      = gg::singleton<CKernelTableTag>;
using HotRegionRegistrySingleton = gg::singleton<HotRegionRegistryTag>;
using BpfLogCbSingleton          = gg::singleton<BpfLogCbTag>;

// ── Node indices (registry order) ─────────────────────────────────────
inline constexpr std::size_t kCKernelTable      = 0;
inline constexpr std::size_t kHotRegionRegistry = 1;
inline constexpr std::size_t kBpfLogCb          = 2;
inline constexpr std::size_t kSingletonCount    = 3;

// ── Init-dependency edges (from, to) = "from's lazy init touches to" ──
// Empty: every production singleton ctor is self-contained.
inline constexpr std::array<std::pair<std::size_t, std::size_t>, 0> kInitEdges{};

// ── COMPILE-TIME PROOF: the production singleton graph is ACYCLIC ─────
static_assert(
    pak::singleton_init_acyclic<kSingletonCount>(kInitEdges),
    "Crucible Meyers-singleton lazy-init graph has a cycle — "
    "static-initialization-order fiasco (S004). Break the cycle or merge "
    "the participating singletons into one initialization unit.");

// ═════════════════════════════════════════════════════════════════════
// ── Surface integrity sentinels ──────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail::self_test {

namespace fg = ::crucible::fixy::grant;
using D      = ::crucible::fixy::dim::DimensionAxis;

// (1) Each registered singleton is a valid grant routing to GlobalState.
static_assert(fg::IsGrantTag<CKernelTableSingleton>);
static_assert(fg::IsGrantTag<HotRegionRegistrySingleton>);
static_assert(fg::IsGrantTag<BpfLogCbSingleton>);
static_assert(fg::which_dim_v<CKernelTableSingleton>      == D::GlobalState);
static_assert(fg::which_dim_v<HotRegionRegistrySingleton> == D::GlobalState);
static_assert(fg::which_dim_v<BpfLogCbSingleton>          == D::GlobalState);

// (2) Distinct tags → distinct singleton grant types (no aliasing).
static_assert(!std::is_same_v<CKernelTableSingleton, HotRegionRegistrySingleton>);
static_assert(!std::is_same_v<HotRegionRegistrySingleton, BpfLogCbSingleton>);
static_assert(!std::is_same_v<CKernelTableSingleton, BpfLogCbSingleton>);

// (3) Detector sanity — a synthetic 2-cycle IS reported as cyclic, and the
//     production edge set IS acyclic.  Proves the proof above is load-
//     bearing (would fire on a real cycle), not vacuously true.
inline constexpr std::array<std::pair<std::size_t, std::size_t>, 2> kCyclicProbe{{{0, 1}, {1, 0}}};
static_assert(pak::singleton_init_has_cycle<2>(kCyclicProbe));
static_assert(pak::singleton_init_acyclic<kSingletonCount>(kInitEdges));

}  // namespace detail::self_test

}  // namespace crucible::safety::singleton_init_graph
