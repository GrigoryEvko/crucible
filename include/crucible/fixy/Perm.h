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

#include <crucible/permissions/FederationPermission.h>  // FIXY-U-071: policy::admit_orgs
#include <crucible/permissions/Permission.h>
#include <crucible/permissions/PermissionFork.h>
#include <crucible/permissions/PermissionInherit.h>

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

}  // namespace crucible::fixy::perm::self_test
