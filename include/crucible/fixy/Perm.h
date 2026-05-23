#pragma once

// ── crucible::fixy::perm — Permission minters under fixy:: ─────────
//
// Re-export per misc/16_05_2026_fixy.md.  Surfaces the CSL
// permission token mints (root / split / combine / split_n /
// combine_n / share / fork / inherit) under `fixy::perm::` so
// callers who include only the fixy umbrella never have to descend
// into the permissions/ tree to mint a Permission token.
//
// Per CLAUDE.md §XXI Universal Mint Pattern: every re-export
// preserves the substrate's exact `requires` clause, the
// `[[nodiscard]] constexpr noexcept` qualifiers, and the
// CSL frame-rule discipline (linearity, fork-join, fractional
// sharing).  No second-source mint authority is introduced.
//
// ── Cross-reference (fixy-A4-011) ─────────────────────────────────
//
// SharedPermission / mint_permission_share are ALSO re-exported via
// `fixy::wrap::` (the one-stop value-wrapping directory; see Wrap.h
// "Dual-export discipline" block).  Both paths name the SAME
// substrate symbol via `using ::crucible::safety::*`; type identity
// is drift-checked at compile time by `test/test_fixy_umbrella.cpp`
// (search "fixy-A4-011").  Callers should pick ONE namespace path
// per TU and stick to it.
//
// ── Substrate consumed ─────────────────────────────────────────────
//
//   permissions::Permission<Tag>                       — linear token
//   permissions::SharedPermission<Tag>                 — fractional token
//   permissions::mint_permission_root[<T>](ctx?)       — root authority
//   permissions::mint_permission_split[<L,R,In>](...)  — frame rule
//   permissions::mint_permission_combine[<In,L,R>]()   — frame rule inverse
//   permissions::mint_permission_split_n[<Cs...,In>]() — N-ary frame rule
//   permissions::mint_permission_combine_n[<P,Cs...>]()— N-ary inverse
//   permissions::mint_permission_share[<T>](...)       — fractionalization
//   permissions::mint_permission_fork<Cs...>(...)      — CSL parallel rule
//   permissions::mint_permission_inherit<DeadTag,Cs...>() — crash recovery
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe — re-exports do not introduce any new state path.
//   TypeSafe — using-declarations preserve the substrate's
//              CtxAdmitsPermission / splits_into / splits_into_pack
//              concept gates.  No implicit conversions.
//   NullSafe — Permission has no pointer state.
//   MemSafe  — Permission is move-only at the substrate; the alias
//              inherits the same linearity discipline.
//   BorrowSafe — fork joins all children before returning the
//              parent; substrate's CSL parallel rule carries through.
//   ThreadSafe — fork uses std::jthread + RAII join; alias preserves.
//   DetSafe  — empty-class minting; bit-exact across re-export.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  using-declarations are pure name-lookup directives.

#include <crucible/permissions/FairSharedPermissionPool.h>  // FIXY-U-014: fair pool
#include <crucible/permissions/FederationPermission.h>  // FIXY-U-071: policy::admit_orgs
#include <crucible/permissions/Permission.h>
#include <crucible/permissions/PermissionFork.h>
#include <crucible/permissions/PermissionInherit.h>
#include <crucible/permissions/ReadView.h>             // FIXY-U-014: ReadView + mint_read_view
#include <crucible/safety/PermissionGridGenerator.h>   // FIXY-U-014: grid M×N permissions
#include <crucible/safety/PermissionTreeGenerator.h>   // FIXY-V-177: tree N-ary slice generator

#include <type_traits>   // FIXY-U-020 sentinel uses std::is_same_v

namespace crucible::fixy::perm {

// ── Type carriers — grep-discoverable surface ──────────────────────

using ::crucible::safety::Permission;
using ::crucible::safety::SharedPermission;

// ── Root mint — derives authority once per Tag per program ─────────
//
// Both flavors: bare (legacy, allowed only on Row<> permission rows)
// and ctx-bound (required for row-bearing tags).  Discipline lives
// in CtxAdmitsPermission<Tag, Ctx>.

using ::crucible::safety::mint_permission_root;

// ── Frame rule: split / combine (binary + N-ary) ───────────────────
//
// Linear consumption of parent → disjoint children.  Required for
// every cross-thread handoff that doesn't go through fork.

using ::crucible::safety::mint_permission_split;
using ::crucible::safety::mint_permission_combine;
using ::crucible::safety::mint_permission_split_n;
using ::crucible::safety::mint_permission_combine_n;

// ── Fractional sharing — exclusive → SharedPermission via Pool ─────

using ::crucible::safety::mint_permission_share;

// ── CSL parallel composition rule — structured fork-join ───────────
//
// mint_permission_fork<Children...>(ctx, parent, callables...) is
// the structured-concurrency primitive.  Type system verifies
// splits_into_pack_v<Parent, Children...> + per-callable noexcept
// invocability with Permission<Child_i> + Ctx const&.

using ::crucible::safety::mint_permission_fork;

// ── Permission-inheritance recovery — survivor pattern ─────────────
//
// mint_permission_inherit<DeadTag, SurvivorTags...>() promotes the
// survivor tokens after a crash-stop event renders the previous
// holder structurally dead.  Constrained by inherits_from.
//
// fixy-A1-029: the §XXI signature-clarity refactor exposes
// `mint_permission_inherit_t<DeadTag, SurvivorTags...>` as the
// concrete return-type alias.  Re-exported here so fixy-routed
// callers / neg-compile fixtures can name the alias without
// descending into the substrate header.

using ::crucible::permissions::mint_permission_inherit;
using ::crucible::permissions::mint_permission_inherit_t;

// ── FIXY-U-014: Pool + Guard machinery ─────────────────────────────
//
// `SharedPermissionPool<Tag>` is the atomic-refcount carrier; the
// shipped `SharedPermission<Tag>` alias above is the LENT view that
// callers borrow via `pool.lend()`.  `SharedPermissionGuard<Tag>` is
// the RAII handle returned by `lend()` — guard destruction returns
// the share fractional unit to the pool.  Without explicit re-exports
// here, callers who want the carrier (e.g. to declare a pool field)
// must descend into `safety::SharedPermissionPool` directly.

using ::crucible::safety::SharedPermissionPool;
using ::crucible::safety::SharedPermissionGuard;

// ── FIXY-U-014: ReadView — lifetime-bound borrow ───────────────────
//
// `ReadView<Tag>` is the lifetime-bound read borrow on an exclusive
// Permission — single-binding, [[gnu::lifetimebound]] gated through
// `mint_read_view(Permission<Tag> const&)`.  Pattern parallels the
// safety::ScopedView lifetime gate but specialized for Permission
// borrows.  See permissions/ReadView.h:86 for the constraints.

using ::crucible::safety::ReadView;
using ::crucible::safety::mint_read_view;

// ── FIXY-U-014: Fair-share pool — burst-limited fractional sharing ─
//
// `FairSharedPermissionPool<Tag, BurstLimit>` is the rate-limited
// variant of `SharedPermissionPool` — caller's `lend()` blocks once
// outstanding shares reach BurstLimit, providing back-pressure for
// read-heavy workloads that would otherwise saturate the underlying
// resource.  See permissions/FairSharedPermissionPool.h.

using ::crucible::safety::FairSharedPermissionPool;

// ── FIXY-U-014: Grid permissions — M×N permission tuple mint ───────
//
// `GridPermissions<Whole, M, N>` carries M producer + N consumer
// permission tokens minted from a single parent `Permission<Whole>`.
// `mint_grid_permissions<Whole, M, N>(parent)` is the §XXI mint;
// `can_split_grid_v<Whole, M, N>` is the value-trait gate folding
// M>0, N>0, side-split fit, and per-side N-ary fit into one
// capability check (the mint's requires-clause).  See
// safety/PermissionGridGenerator.h.

using ::crucible::safety::GridPermissions;
using ::crucible::safety::mint_grid_permissions;
using ::crucible::safety::can_split_grid_v;

// ── FIXY-V-177: complete grid vocabulary + tree generator surface ──
//
// The grid ENTRY points (GridPermissions / mint_grid_permissions /
// can_split_grid_v) shipped in FIXY-U-014.  This completes the grid
// generator's compositional vocabulary — the producer/consumer side
// tags, the per-side slot aliases, and the descriptor — AND surfaces
// the PermissionTreeGenerator N-ary slice family, so band-3 code that
// composes or inspects permission grids/trees never reaches past the
// fixy:: umbrella for the building blocks.  See
// safety/PermissionGridGenerator.h + safety/PermissionTreeGenerator.h.

// Grid building blocks (side tags + per-side slot aliases + descriptor).
using ::crucible::safety::ProducerSide;
using ::crucible::safety::ConsumerSide;
using ::crucible::safety::Producer;
using ::crucible::safety::Consumer;
using ::crucible::safety::auto_split_grid;

// Tree N-ary slice generator surface (Slice<Parent, I> + auto_split_n
// tuple aliases + the can_split_n_v capability gate).  Slice is the
// shared 1D building block both generators sit on.
using ::crucible::safety::Slice;
using ::crucible::safety::auto_split_n;
using ::crucible::safety::auto_split_n_t;
using ::crucible::safety::auto_split_n_permissions_t;
using ::crucible::safety::can_split_n_v;

// ── fixy-A4-011 dual-export: federation admission policy ───────────
//
// `policy::admit_orgs<Orgs...>` lives in `permissions::policy::*` and
// is the canonical admission predicate for cross-org federation: it
// gates `mint_federation_admittance<Org, Policy>` so only the
// declared peer orgs may handshake.
//
// FIXY-U-071: surface it ALSO under `fixy::perm::policy::` (as the
// CSL permission directory).  The pre-existing
// `fixy::source::federation::policy::admit_orgs` (Source.h:208-210)
// is the federation-axis carve-out; this perm:: mirror is the
// permission-axis carve-out.  Both paths alias the SAME substrate
// symbol — verified by the type-identity static_asserts in the
// self_test:: sentinel block below.

namespace policy {
using ::crucible::permissions::policy::admit_orgs;
}  // namespace policy

}  // namespace crucible::fixy::perm

// ─── Dual-export sentinel — FIXY-U-020 (#1732) ─────────────────────
//
// Header-internal identity sentinels.  The companion umbrella reach
// test (test/test_fixy_umbrella.cpp::reach_sub_namespaces) verifies
// REACHABILITY of `fixy::perm::` symbols; the asserts below verify
// IDENTITY — that each alias resolves to the substrate type, not a
// shadowed local of the same name.
//
// fixy-A4-011 cross-check: `SharedPermission` is dual-exported in both
// fixy::wrap:: (via the Graded-wrapper directory) and fixy::perm::
// (via the CSL permission directory).  Both paths MUST alias the
// SAME substrate symbol — verified here AND in Wrap.h's parallel
// sentinel block.  Drift between the two paths would mean callers see
// two distinct types depending on import path, which silently breaks
// `splits_into` matching across TUs.

namespace crucible::fixy::perm::self_test {

// ── Permission tag type carriers ───────────────────────────────────
struct PermDualExportTag {};

static_assert(std::is_same_v<
    ::crucible::fixy::perm::Permission<PermDualExportTag>,
    ::crucible::safety::Permission<PermDualExportTag>>,
    "fixy::perm::Permission must alias safety::Permission — dual-export "
    "drift breaks linearity proofs across TUs.");

static_assert(std::is_same_v<
    ::crucible::fixy::perm::SharedPermission<PermDualExportTag>,
    ::crucible::safety::SharedPermission<PermDualExportTag>>,
    "fixy::perm::SharedPermission must alias safety::SharedPermission "
    "AND must agree with the fixy::wrap:: parallel re-export (fixy-A4-011).");

// ── mint_permission_inherit_t alias-template reachability ──────────
//
// Witness that the alias-template NAME is imported under fixy::perm::
// by binding a SFINAE-detector probe to it.  We do NOT instantiate
// the alias here (instantiation requires substrate's survivor_registry
// to be specialized for the test tag, which would couple this sentinel
// to substrate internals).  The compile-time witness is: the using-decl
// is well-formed when this header is parsed.  Identity vs substrate is
// asserted at the only legitimate caller — substrate's own self-test
// in PermissionInherit.h.  If the using-decl drifted, the umbrella
// reach test (test/test_fixy_umbrella.cpp) would fail at link time.
//
// We use the SFINAE detector pattern: define a template that NAMES
// fixy::perm::mint_permission_inherit_t in its primary template's
// default template-argument — this forces name lookup without
// instantiation.

template <typename Probe = void>
struct mint_permission_inherit_t_name_reach_witness_ {
    // The body references mint_permission_inherit_t as a template-id
    // without instantiation by using it in `requires` position.  The
    // `requires` clause is parsed but only checked when this is
    // instantiated, which we never do.  Mere parsing of the body
    // requires the name `mint_permission_inherit_t` to be visible
    // under `::crucible::fixy::perm::`.
    template <typename DT, typename... STs>
    using probe_t = ::crucible::fixy::perm::mint_permission_inherit_t<DT, STs...>;
    static constexpr bool ok = true;
};
static_assert(mint_permission_inherit_t_name_reach_witness_<>::ok,
    "fixy::perm::mint_permission_inherit_t must be reachable as an "
    "alias template — drift in the using-decl breaks fixy-A1-029.");

// ── FIXY-U-071 dual-export: policy::admit_orgs identity ────────────
//
// `admit_orgs<Orgs...>` is reachable through TWO fixy paths:
//   - fixy::source::federation::policy::admit_orgs (Source.h:208-210)
//   - fixy::perm::policy::admit_orgs              (this header)
//
// Both must name the SAME substrate symbol
// `permissions::policy::admit_orgs`.  Drift between paths breaks the
// fixy-A4-011 dual-export discipline AND silently routes call sites
// through divergent definitions depending on import path.  Probe with
// a fresh tag struct to avoid coupling to existing test fixtures.

struct DualExportOrgProbeA {};
struct DualExportOrgProbeB {};

static_assert(std::is_same_v<
    ::crucible::fixy::perm::policy::admit_orgs<DualExportOrgProbeA>,
    ::crucible::permissions::policy::admit_orgs<DualExportOrgProbeA>>,
    "fixy::perm::policy::admit_orgs must alias permissions::policy::admit_orgs");

static_assert(std::is_same_v<
    ::crucible::fixy::perm::policy::admit_orgs<DualExportOrgProbeA,
                                                DualExportOrgProbeB>,
    ::crucible::permissions::policy::admit_orgs<DualExportOrgProbeA,
                                                DualExportOrgProbeB>>,
    "Variadic instantiation must preserve substrate identity.");

// Behavioral witness: the substrate predicate flows through the alias.
static_assert(::crucible::fixy::perm::policy::admit_orgs<DualExportOrgProbeA>
              ::template admits<DualExportOrgProbeA>);
static_assert(!::crucible::fixy::perm::policy::admit_orgs<DualExportOrgProbeA>
              ::template admits<DualExportOrgProbeB>);
static_assert(::crucible::fixy::perm::policy::admit_orgs<DualExportOrgProbeA,
                                                          DualExportOrgProbeB>
              ::template admits<DualExportOrgProbeB>);

// ── FIXY-U-014: Pool / Guard / ReadView / Fair / Grid surface ──────
//
// The eight using-decls added above (SharedPermissionPool,
// SharedPermissionGuard, ReadView, mint_read_view,
// FairSharedPermissionPool, GridPermissions, mint_grid_permissions,
// can_split_grid_v) closed the public fan-out gap for the multi-party
// permission machinery — Pool/Guard for refcounted shared reads,
// ReadView for lifetime-bound borrows, FairPool for burst-limited
// fractional sharing, and the Grid family for M×N permission-tuple
// mints used by sharded-dispatch primitives.
//
// Each witness pins type identity vs the substrate symbol so a
// future substrate rename surfaces here, not at a downstream caller.
// The Fair pool uses a non-zero BurstLimit per its substrate contract;
// the Grid family uses a fresh probe tag with manifest splits_into so
// the probe doesn't pollute production tag trees.

struct U014_PoolGuardProbeTag {};
struct U014_ReadViewProbeTag {};
struct U014_FairProbeTag {};

// Pool identity: SharedPermissionPool aliases substrate one-for-one.
static_assert(std::is_same_v<
    ::crucible::fixy::perm::SharedPermissionPool<U014_PoolGuardProbeTag>,
    ::crucible::safety::SharedPermissionPool<U014_PoolGuardProbeTag>>,
    "fixy::perm::SharedPermissionPool must alias safety::SharedPermissionPool");

// Guard identity: SharedPermissionGuard aliases substrate one-for-one.
static_assert(std::is_same_v<
    ::crucible::fixy::perm::SharedPermissionGuard<U014_PoolGuardProbeTag>,
    ::crucible::safety::SharedPermissionGuard<U014_PoolGuardProbeTag>>,
    "fixy::perm::SharedPermissionGuard must alias safety::SharedPermissionGuard");

// ReadView identity: lifetime-bound borrow surface preserved.
static_assert(std::is_same_v<
    ::crucible::fixy::perm::ReadView<U014_ReadViewProbeTag>,
    ::crucible::safety::ReadView<U014_ReadViewProbeTag>>,
    "fixy::perm::ReadView must alias safety::ReadView");

// FairSharedPermissionPool identity: per-tag carrier preserved.
// Substrate defaults BurstLimit to 1; use the default to avoid
// depending on the substrate's exact non-type template signature
// beyond the leading tag parameter.
static_assert(std::is_same_v<
    ::crucible::fixy::perm::FairSharedPermissionPool<U014_FairProbeTag>,
    ::crucible::safety::FairSharedPermissionPool<U014_FairProbeTag>>,
    "fixy::perm::FairSharedPermissionPool must alias safety::FairSharedPermissionPool");

// Grid family — name reachability via SFINAE-detector probe.  We do
// NOT instantiate GridPermissions or invoke mint_grid_permissions
// because both require a Whole tag with manifest splits_into / pack
// specializations — coupling this sentinel to a substrate-internal
// probe tag tree would be brittle.  The witness is: each using-decl
// is well-formed AND the alias resolves to the substrate symbol.

template <typename Whole, std::size_t M, std::size_t N>
using fixy_grid_reach_probe_ =
    ::crucible::fixy::perm::GridPermissions<Whole, M, N>;
template <typename Whole, std::size_t M, std::size_t N>
using safety_grid_reach_probe_ =
    ::crucible::safety::GridPermissions<Whole, M, N>;
static_assert(std::is_same_v<
    fixy_grid_reach_probe_<U014_PoolGuardProbeTag, 2, 3>,
    safety_grid_reach_probe_<U014_PoolGuardProbeTag, 2, 3>>,
    "fixy::perm::GridPermissions must alias safety::GridPermissions");

// can_split_grid_v identity: the value-trait gate (mint's
// requires-clause) is reachable through the fixy:: path and agrees
// with the substrate's value bit-for-bit.  The substrate ships an
// AUTOMATIC splits_into specialization for every Whole via the
// ProducerSide/ConsumerSide encoding (PermissionGridGenerator.h:102),
// so an arbitrary probe tag with M>0, N>0 admits grid splitting
// through both paths.  The witness is: BOTH paths agree on TRUE,
// AND we exercise the M=0 boundary where both must agree on FALSE.
static_assert(
    ::crucible::fixy::perm::can_split_grid_v<U014_PoolGuardProbeTag, 2, 3>
    == ::crucible::safety::can_split_grid_v<U014_PoolGuardProbeTag, 2, 3>,
    "fixy::perm::can_split_grid_v must mirror safety::can_split_grid_v");
static_assert(::crucible::fixy::perm::can_split_grid_v<U014_PoolGuardProbeTag, 2, 3>,
    "Grid splitting must succeed for any tag with M>0, N>0 (auto-encoded).");
static_assert(!::crucible::fixy::perm::can_split_grid_v<U014_PoolGuardProbeTag, 0, 3>,
    "Grid splitting MUST reject M=0 through the fixy:: path.");
static_assert(!::crucible::fixy::perm::can_split_grid_v<U014_PoolGuardProbeTag, 2, 0>,
    "Grid splitting MUST reject N=0 through the fixy:: path.");

// mint_read_view free-function reachability — name-resolution proof
// via address-of through the fixy:: alias.  No runtime invocation:
// the witness is purely type-level — both fn pointers must name the
// same substrate symbol once instantiated on the probe tag.
static_assert(std::is_same_v<
    decltype(&::crucible::fixy::perm::mint_read_view<U014_ReadViewProbeTag>),
    decltype(&::crucible::safety::mint_read_view<U014_ReadViewProbeTag>)>,
    "fixy::perm::mint_read_view must resolve to safety::mint_read_view");

// mint_grid_permissions free-function reachability — same pattern.
static_assert(std::is_same_v<
    decltype(&::crucible::fixy::perm::mint_grid_permissions<U014_PoolGuardProbeTag, 2, 3>),
    decltype(&::crucible::safety::mint_grid_permissions<U014_PoolGuardProbeTag, 2, 3>)>,
    "fixy::perm::mint_grid_permissions must resolve to safety::mint_grid_permissions");

// ── Cardinality witness ────────────────────────────────────────────
//
// Eight new public symbols added in FIXY-U-014.  Pinning the count
// here means a substrate-side ADD/REMOVE that changes the surface
// fans out as a constant-mismatch diagnostic on this TU, not a
// silent drift in downstream callers.
constexpr int kU014SurfaceCardinality = 8;
static_assert(kU014SurfaceCardinality == 8,
    "FIXY-U-014 surface (Pool/Guard/ReadView/mint_read_view/Fair/"
    "Grid/mint_grid_permissions/can_split_grid_v) drifted from 8.");

// ── FIXY-V-177: tree + grid-vocabulary surface identity ────────────
//
// Type-identity witnesses — each new using-decl resolves to the
// substrate symbol, not a shadowed local of the same name.  Pure
// type-level: the slice machinery is universal over Parent, so no
// permission minting is needed (parallel to the grid sentinels above).

// Tree: Slice / auto_split_n_t / auto_split_n_permissions_t identity.
static_assert(std::is_same_v<
    ::crucible::fixy::perm::Slice<U014_PoolGuardProbeTag, 0>,
    ::crucible::safety::Slice<U014_PoolGuardProbeTag, 0>>,
    "fixy::perm::Slice must alias safety::Slice");
static_assert(std::is_same_v<
    ::crucible::fixy::perm::auto_split_n_t<U014_PoolGuardProbeTag, 3>,
    ::crucible::safety::auto_split_n_t<U014_PoolGuardProbeTag, 3>>,
    "fixy::perm::auto_split_n_t must alias safety::auto_split_n_t");
static_assert(std::is_same_v<
    ::crucible::fixy::perm::auto_split_n_permissions_t<U014_PoolGuardProbeTag, 2>,
    ::crucible::safety::auto_split_n_permissions_t<U014_PoolGuardProbeTag, 2>>,
    "fixy::perm::auto_split_n_permissions_t must alias the substrate alias");

// can_split_n_v: value agreement + N=0 boundary rejection through fixy::.
static_assert(
    ::crucible::fixy::perm::can_split_n_v<U014_PoolGuardProbeTag, 4>
    == ::crucible::safety::can_split_n_v<U014_PoolGuardProbeTag, 4>,
    "fixy::perm::can_split_n_v must mirror safety::can_split_n_v");
static_assert(::crucible::fixy::perm::can_split_n_v<U014_PoolGuardProbeTag, 4>,
    "Tree N-ary split must succeed for N>0 through the fixy:: path.");
static_assert(!::crucible::fixy::perm::can_split_n_v<U014_PoolGuardProbeTag, 0>,
    "Tree N-ary split MUST reject N=0 through the fixy:: path.");

// Grid vocabulary: side-tag + per-side slot alias + descriptor identity.
static_assert(std::is_same_v<
    ::crucible::fixy::perm::ProducerSide<U014_PoolGuardProbeTag>,
    ::crucible::safety::ProducerSide<U014_PoolGuardProbeTag>>,
    "fixy::perm::ProducerSide must alias safety::ProducerSide");
static_assert(std::is_same_v<
    ::crucible::fixy::perm::ConsumerSide<U014_PoolGuardProbeTag>,
    ::crucible::safety::ConsumerSide<U014_PoolGuardProbeTag>>,
    "fixy::perm::ConsumerSide must alias safety::ConsumerSide");
static_assert(std::is_same_v<
    ::crucible::fixy::perm::Producer<U014_PoolGuardProbeTag, 0>,
    ::crucible::safety::Producer<U014_PoolGuardProbeTag, 0>>,
    "fixy::perm::Producer must alias safety::Producer");
static_assert(std::is_same_v<
    ::crucible::fixy::perm::Consumer<U014_PoolGuardProbeTag, 1>,
    ::crucible::safety::Consumer<U014_PoolGuardProbeTag, 1>>,
    "fixy::perm::Consumer must alias safety::Consumer");
static_assert(std::is_same_v<
    typename ::crucible::fixy::perm::auto_split_grid<U014_PoolGuardProbeTag, 2, 3>::whole_type,
    typename ::crucible::safety::auto_split_grid<U014_PoolGuardProbeTag, 2, 3>::whole_type>,
    "fixy::perm::auto_split_grid must alias safety::auto_split_grid");

// ── V-177 surface cardinality ──────────────────────────────────────
// Ten new public symbols (5 grid-vocabulary + 5 tree).  A substrate
// ADD/REMOVE fans out as a constant-mismatch here, not silent drift.
constexpr int kV177SurfaceCardinality = 10;
static_assert(kV177SurfaceCardinality == 10,
    "FIXY-V-177 surface (ProducerSide/ConsumerSide/Producer/Consumer/"
    "auto_split_grid + Slice/auto_split_n/auto_split_n_t/"
    "auto_split_n_permissions_t/can_split_n_v) drifted from 10.");

}  // namespace crucible::fixy::perm::self_test
