#pragma once

// ── crucible::fixy::perm — Permission minters under fixy:: ─────────
//
// Phase C re-export per misc/16_05_2026_fixy.md.  Surfaces the CSL
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

#include <crucible/permissions/Permission.h>
#include <crucible/permissions/PermissionFork.h>
#include <crucible/permissions/PermissionInherit.h>

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

using ::crucible::permissions::mint_permission_inherit;

}  // namespace crucible::fixy::perm
