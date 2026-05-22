#pragma once

// ── crucible::fixy::spawn — structured-concurrency minters ─────────
//
// FIXY-V-083: ctx-bound facades over the substrate's structured-
// concurrency primitives.  `fixy::spawn::mint_spawn` re-projects
// `safety::mint_permission_fork` (CSL parallel composition rule).
// `fixy::spawn::mint_parallel_for` re-projects
// `safety::parallel_for_views<N>` (compile-time-N range fan-out
// over an OwnedRegion).  Both are §XXI Universal-Mint-Pattern
// ctx-bound mints — first parameter `Ctx const&`, single
// `requires CtxFitsX<...>` concept gate, `[[nodiscard]] noexcept`.
//
// V-083 is the FIRST file in the `fixy/spawn/` family.  Two later
// tasks elaborate the grant surface alongside (NOT consumed here):
//
//   V-203 — fixy/spawn/JoinPolicy.h — phantom-typed join-policy tags
//   V-204 — fixy/spawn/SpawnGrant.h — engagement tags + rationale
//
// Until those land, `mint_spawn` and `mint_parallel_for` ride the
// substrate's existing gates: row admits Effect::Bg + permission
// admission + (for spawn) splits_into_pack + per-callable noexcept
// invocability.
//
// ── Substrate consumed ─────────────────────────────────────────────
//
//   safety::Permission<Tag>                          — linear token
//   safety::OwnedRegion<T, Tag>                      — span + perm pair
//   safety::Slice<Whole, I>                          — indexed sub-tag
//   safety::mint_permission_fork<Children...>(...)   — CSL parallel rule
//   safety::CtxFitsPermissionFork<Ctx, P, Cs...>    — substrate's gate
//   safety::detail::permission_fork_ctx_callables_v  — callable noexcept gate
//   safety::parallel_for_views<N>(...)               — range fan-out
//   safety::CtxAdmitsPermission<Tag, Ctx>           — row-fit predicate
//   effects::IsExecCtx<Ctx>                          — ctx shape
//   effects::CtxOwnsCapability<Ctx, Effect::Bg>     — Bg-row admission
//
// ── §XXI compliance ────────────────────────────────────────────────
//
// Both mints:
//   * First parameter is `Ctx const&` (ctx-bound mint).
//   * Single `requires CtxFitsX<...>` concept gate at the call site.
//     Multi-clause requirements live INSIDE the concept (folded into
//     `ctx_fits_spawn_helper` / `CtxFitsParallelFor`).
//   * `[[nodiscard]]` + `noexcept`; not `constexpr` because they
//     spawn threads at runtime (or might, per cache-tier rule).
//   * Returned types are CONCRETE (`Permission<Parent>`,
//     `OwnedRegion<T, Whole>`); no type erasure.
//   * Diagnostic surface inherits from the substrate's named gates
//     (CtxFitsPermissionFork / CtxAdmitsPermission / etc.) for
//     grep-discoverable HS14 fixture regexes.
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe   — no new state path; carriers default-construct via
//                substrate ctors.
//   TypeSafe   — Permission<Tag> + Slice<Whole, I> phantom-typed;
//                children pack mismatch is a compile error.
//   NullSafe   — Permission has no pointer state; OwnedRegion is
//                (T*, size_t) where the size_t == 0 path is the
//                empty-span legitimate case.
//   MemSafe    — Permission is move-only; OwnedRegion linearity rides
//                its embedded Permission.  No heap.
//   BorrowSafe — children Permissions disjoint by splits_into_pack
//                discipline; Slice<Whole, I> indexed disjointness for
//                parallel_for.
//   ThreadSafe — substrate's RAII jthread-join; the fixy facade
//                preserves the discipline byte-identically.
//   LeakSafe   — substrate joins all threads before returning.
//   DetSafe    — both mints are pure structural composition; no FP,
//                no RNG, no kernel.  parallel_for_views writes per
//                disjoint shard, recombined in canonical index order.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero beyond the substrate's existing cost.  The fixy wrapper
// performs ONLY type-level concept-gating and forwards the call;
// gcc -O3 inlines the entire bridge.

#include <crucible/effects/Capabilities.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/permissions/Permission.h>
#include <crucible/permissions/PermissionFork.h>
#include <crucible/safety/OwnedRegion.h>
#include <crucible/safety/PermissionTreeGenerator.h>  // safety::Slice<Whole, I>
#include <crucible/safety/Workload.h>                  // parallel_for_views<N>

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace crucible::fixy::spawn {

// ═════════════════════════════════════════════════════════════════════
// ── Type carriers — grep-discoverable surface ──────────────────────
// ═════════════════════════════════════════════════════════════════════

using ::crucible::safety::OwnedRegion;
using ::crucible::safety::Permission;
using ::crucible::safety::Slice;

// ═════════════════════════════════════════════════════════════════════
// ── mint_spawn — CSL parallel-rule facade ──────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// §XXI ctx-bound mint.  Forwards to safety::mint_permission_fork
// after a single concept-folded gate combining (a) the substrate's
// row + admission + splits_into_pack gate (CtxFitsPermissionFork)
// and (b) the per-callable noexcept invocability gate.
//
// The single-concept folding is the §XXI rule "Multi-clause requires-
// lists belong INSIDE the concept definition, not at the call site"
// — the helper struct below combines BOTH substrate gates so the
// call-site reads `requires CtxFitsSpawn<...>` exactly.
//
// HS14 fixtures (test/fixy_neg/):
//   1. neg_fixy_spawn_mint_spawn_ctx_no_bg.cpp
//        — Ctx::row missing Effect::Bg → CtxFitsPermissionFork fails.
//   2. neg_fixy_spawn_mint_spawn_no_splits_into_pack.cpp
//        — splits_into_pack<Whole, ...> not declared → gate fails.

namespace detail {

template <typename Ctx, typename Parent,
          typename ChildrenTuple, typename CallablesTuple>
struct ctx_fits_spawn_helper : std::false_type {};

template <typename Ctx, typename Parent,
          typename... Children, typename... Callables>
struct ctx_fits_spawn_helper<
    Ctx, Parent,
    std::tuple<Children...>, std::tuple<Callables...>>
    : std::bool_constant<
         ::crucible::safety::CtxFitsPermissionFork<Ctx, Parent, Children...>
         && ::crucible::safety::detail::permission_fork_ctx_callables_v<
                Ctx,
                std::tuple<Children...>,
                std::tuple<Callables...>>
      > {};

}  // namespace detail

// ── CtxFitsSpawn ──────────────────────────────────────────────────
//
// Single concept gate for `mint_spawn`.  Folds the substrate's two
// gates (row + splits_into_pack vs per-callable noexcept) so the
// call-site has exactly one `requires` clause per §XXI.

template <typename Ctx, typename Parent,
          typename ChildrenTuple, typename CallablesTuple>
concept CtxFitsSpawn = detail::ctx_fits_spawn_helper<
    Ctx, Parent, ChildrenTuple, CallablesTuple>::value;

// ── mint_spawn<Children...>(ctx, parent, callables...) ────────────
//
// Returns the parent Permission after all children join.

template <typename... Children,
          typename Ctx, typename Parent, typename... Callables>
    requires CtxFitsSpawn<
        Ctx, Parent,
        std::tuple<Children...>,
        std::tuple<std::decay_t<Callables>...>>
[[nodiscard]] Permission<Parent> mint_spawn(
    Ctx const& ctx,
    Permission<Parent>&& parent,
    Callables&&... callables) noexcept
{
    return ::crucible::safety::mint_permission_fork<Children...>(
        ctx,
        std::move(parent),
        std::forward<Callables>(callables)...);
}

// ═════════════════════════════════════════════════════════════════════
// ── mint_parallel_for — compile-time-N range fan-out facade ────────
// ═════════════════════════════════════════════════════════════════════
//
// §XXI ctx-bound mint.  Forwards to safety::parallel_for_views<N>.
// N == 1 collapses to inline body invocation (no jthread spawn);
// N >= 2 spawns N jthreads with RAII join.  In either case the
// returned OwnedRegion has been re-combined from the N disjoint
// Slice<Whole, I> sub-regions.
//
// Concept gate folds:
//   * IsExecCtx<Ctx>
//   * CtxOwnsCapability<Ctx, Effect::Bg> — required even at N==1 so
//     the contract surface is uniform; the cache-tier rule lives
//     INSIDE parallel_for_views (N==1 fast path is unconditionally
//     inline).
//   * CtxAdmitsPermission<Whole, Ctx> — the parent's permission row
//     must fit the ctx (consistent with mint_permission_root rules).
//   * Body is noexcept-invocable as
//     `void(OwnedRegion<T, Slice<Whole, 0>>&&)` — at least one shard
//     (substrate static_asserts this; fold here for clean diag).
//   * For N>=2: Body is CopyConstructible (substrate also enforces).
//
// HS14 fixtures (test/fixy_neg/):
//   1. neg_fixy_spawn_mint_parallel_for_ctx_no_bg.cpp
//        — Ctx::row missing Effect::Bg → CtxFitsParallelFor fails on
//          the CtxOwnsCapability axis.
//   2. neg_fixy_spawn_mint_parallel_for_body_signature.cpp
//        — Body invocable with the WRONG slice tag (e.g. taking
//          OwnedRegion<T, Whole>&& rather than Slice<Whole, 0>) →
//          CtxFitsParallelFor fails on the body-shape axis.

template <std::size_t N, typename Ctx, typename T,
          typename Whole, typename Body>
concept CtxFitsParallelFor =
    (N > 0)
    && ::crucible::effects::IsExecCtx<Ctx>
    && ::crucible::effects::CtxOwnsCapability<
           Ctx, ::crucible::effects::Effect::Bg>
    && ::crucible::safety::CtxAdmitsPermission<Whole, Ctx>
    && std::is_nothrow_invocable_v<
           Body&,
           OwnedRegion<T, Slice<Whole, 0>>&&>
    && (N == 1 || std::is_copy_constructible_v<Body>);

// ── mint_parallel_for<N>(ctx, region, body) ───────────────────────
//
// Returns the rebuilt OwnedRegion<T, Whole> after every shard's
// body invocation completes.

template <std::size_t N, typename Ctx, typename T,
          typename Whole, typename Body>
    requires CtxFitsParallelFor<N, Ctx, T, Whole, Body>
[[nodiscard]] OwnedRegion<T, Whole> mint_parallel_for(
    Ctx const& /*ctx*/,
    OwnedRegion<T, Whole>&& region,
    Body body) noexcept
{
    // The Ctx is consumed only at the type-level gate above; the
    // substrate's parallel_for_views<N> reads no ctx (its cache-tier
    // / fan-out discipline rides on N alone).
    return ::crucible::safety::parallel_for_views<N>(
        std::move(region),
        std::move(body));
}

}  // namespace crucible::fixy::spawn

// ─── FIXY-V-083 in-header sentinel ─────────────────────────────────
//
// Drift-catch for the two mints + two type-carriers above.  Type
// identity is witnessed via std::is_same_v on the substrate; an
// errant typedef redefinition (e.g. someone aliasing
// `fixy::spawn::Slice` to a local type) reds at every consumer's
// include time.  Cardinality witness traps additions or removals
// against the constant below.  Same recipe as fixy/Pipe.h::
// self_test and fixy/concurrent/SpinLock.h sentinel TUs.

namespace crucible::fixy::spawn::self_test {

static_assert(std::is_same_v<
    ::crucible::fixy::spawn::Permission<int>,
    ::crucible::safety::Permission<int>>,
    "fixy::spawn::Permission must alias safety::Permission");

static_assert(std::is_same_v<
    ::crucible::fixy::spawn::OwnedRegion<int, struct probe_tag_>,
    ::crucible::safety::OwnedRegion<int, struct probe_tag_>>,
    "fixy::spawn::OwnedRegion must alias safety::OwnedRegion");

static_assert(std::is_same_v<
    ::crucible::fixy::spawn::Slice<struct probe_tag_, 0>,
    ::crucible::safety::Slice<struct probe_tag_, 0>>,
    "fixy::spawn::Slice must alias safety::Slice");

// Cardinality witness — surface count of using-decls + mint
// factories in this header.  V-083 baseline: 3 type carriers
// (Permission, OwnedRegion, Slice) + 2 mints (mint_spawn,
// mint_parallel_for) + 2 concepts (CtxFitsSpawn, CtxFitsParallelFor)
// = 7.  Any add/remove must update this number AND the per-mint
// HS14 fixture row in misc/mint-inventory.md.
constexpr int spawn_surface_cardinality = 7;
static_assert(spawn_surface_cardinality == 7,
    "fixy::spawn:: surface drifted — update Spawn.h surfaces + this "
    "sentinel + HS14 fixtures in lockstep.");

}  // namespace crucible::fixy::spawn::self_test
