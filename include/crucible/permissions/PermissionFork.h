#pragma once

// ── crucible::safety::mint_permission_fork — CSL parallel rule encoding ───
//
// Direct C++ encoding of Concurrent Separation Logic's parallel
// composition rule:
//
//   {P1} C1 {Q1}    {P2} C2 {Q2}                         ... {Pn} Cn {Qn}
//   ────────────────────────────────────────────────────────────────────────
//   {P1 * P2 * ... * Pn} (C1 || C2 || ... || Cn) {Q1 * Q2 * ... * Qn}
//
// mint_permission_fork takes a row-admitted ExecCtx plus a parent
// Permission, splits it into N disjoint children via splits_into_pack,
// and invokes each child body as
//
//   Callable_i(Permission<Child_i>&&, Ctx const&)
//
// The ctx must admit Effect::Bg: fork/join is a background-execution
// effect even when the cache-tier rule chooses inline execution.  If
// concurrent::ParallelismRule recommends Sequential for the ctx's
// workload budget (CLAUDE.md §IX / GAPS-034), bodies run inline in
// child order; otherwise the implementation spawns one std::jthread
// per child, joins all threads (RAII), and returns the parent
// Permission.  The parent is the SAME Tag the caller had before — but
// every child has independently mutated its own region, and the type
// system proved at compile time that no two children touched the same
// region.
//
//   Axiom coverage: ThreadSafe + BorrowSafe + the CSL parallel rule
//                   itself.
//   Runtime cost:   Either inline child calls (sequential cache-tier
//                   decision) or exactly one std::jthread spawn + join
//                   per child.  The Permission tokens are zero-byte
//                   (EBO).
//
// ─── Why this is THE structured-concurrency primitive ──────────────
//
// Compare to raw thread spawning:
//   * Raw `std::jthread` requires manual coordination (atomic counters,
//     spin-loops on conditions) for "all done" — error-prone, the
//     SHARDED-test deadlock that motivated this file is exactly that
//     pattern's failure mode.
//   * mint_permission_fork joins on RAII (jthread destructor in scope-exit
//     order); the parent Permission is returned only AFTER all children
//     have finished.  No counter, no condition, no spin.
//
// Compare to std::async / std::future:
//   * std::async returns a std::future which the caller must remember
//     to .get() — easy to leak (future destructor blocks if not got).
//   * mint_permission_fork is a single expression; the join is automatic.
//
// Compare to OpenMP / TBB parallel_for:
//   * Those rely on runtime task-stealing pools (overhead) and don't
//     plumb permissions through to the bodies.
//   * mint_permission_fork is zero-overhead beyond either the inline body
//     calls or jthread spawn/join, and each child receives a TYPED
//     permission proving its disjoint region access.
//
// ─── Constraints ────────────────────────────────────────────────────
//
//   * Ctx must satisfy effects::IsExecCtx and its row_type must contain
//     effects::Effect::Bg.
//   * splits_into_pack_v<Parent, Children...> must be true (declarative
//     manifest in safety/Permission.h).
//   * sizeof...(Children) == sizeof...(Callables).
//   * Each Callable_i must be invocable as
//     Callable_i(Permission<Child_i>&&, Ctx const&) — it CONSUMES its
//     child Permission token and inherits the row-admitted context.
//   * Callables should be noexcept (Crucible's -fno-exceptions rule);
//     uncaught exceptions in jthread bodies call std::terminate.
//   * Children must NOT outlive the call (jthread joins before return).
//
// ─── Performance ────────────────────────────────────────────────────
//
//   For a cache-resident ctx budget, the fork degrades to inline child
//   calls and avoids over-parallelizing work that fits in one core's
//   L1/L2.  For larger budgets, N child jthreads = N pthread_create +
//   N pthread_join.  Spawn cost ~5-15 µs per thread on Linux.  This is
//   the right primitive for "few threads, long bodies" — NOT for
//   "thousands of micro-tasks" (use ChaseLevDeque + ThreadPool for
//   that, see SEPLOG-C4).
//
// ─── Example ────────────────────────────────────────────────────────
//
//   // 1. Tag tree
//   namespace work {
//       struct Whole {};
//       struct Left {};
//       struct Right {};
//   }
//   namespace crucible::safety {
//       template <> struct splits_into_pack<work::Whole, work::Left, work::Right>
//                    : std::true_type {};
//   }
//
//   // 2. Mint and fork:
//   auto whole = mint_permission_root<work::Whole>();
//
//   auto returned_whole = mint_permission_fork<work::Left, work::Right>(
//       PermissionForkSpawnCtx{},
//       std::move(whole),
//       [](Permission<work::Left>, PermissionForkSpawnCtx const&) noexcept {
//           /* mutate left region */
//       },
//       [](Permission<work::Right>, PermissionForkSpawnCtx const&) noexcept {
//           /* mutate right region */
//       }
//   );
//
//   // 3. After return, both threads have joined; both regions have been
//   //    touched in parallel; type system proved disjointness; the
//   //    `returned_whole` Permission is again exclusive in this scope.

#include <crucible/Platform.h>
#include <crucible/concurrent/ExecCtxBridge.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/ctrl/Throws.h>
#include <crucible/permissions/Permission.h>

#include <array>
#include <cstddef>
#include <cstdlib>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>

// ── fixy-A1-010: honor noexcept against std::jthread's throwing ctor ──
//
// `mint_permission_fork` (below) is declared `noexcept` because it
// is the user-facing structured-concurrency primitive — callers
// chain CSL-typed handoffs through it and must know it cannot
// propagate exceptions.  Internally it spawns `std::jthread`s
// whose constructors are NOT `noexcept`: under the hood pthread_
// create can fail with EAGAIN / ENOMEM / EINVAL and libstdc++
// throws `std::system_error`.
//
// Two failure modes are possible if we did nothing:
//
//   * `-fno-exceptions` set: libstdc++ rewrites the throw to
//     `_GLIBCXX_THROW_OR_ABORT` → `std::abort`.  Noexcept honored,
//     Crucible's resource-fails-abort policy honored.
//   * `-fexceptions` set: `std::system_error` propagates to the
//     noexcept boundary and the runtime calls `std::terminate`.
//     Noexcept technically honored (terminate IS a noexcept end
//     state), but the failure differs from Crucible's `std::abort`
//     contract and bypasses any `crucible_abort()` flushing logic.
//
// We do not rely on the build flag.  `permission_fork_spawn_`
// (below) wraps the `std::jthread`-constructing array initializer in a
// `try` / `catch (...)` and converts any caught exception into
// `std::abort()`.  The catch is a no-op under `-fno-exceptions`
// (the throw was already rewritten), and a defensive abort under
// `-fexceptions`.  The noexcept declaration is honored at the
// source level, not at the build-flag level.
//
// The catch must be `(...)` rather than `(std::system_error const&)`
// because under future libstdc++ versions or non-libstdc++ runtimes
// the exception type may change; the contract is "any exception
// from std::jthread construction becomes std::abort", not
// specifically "std::system_error".

namespace crucible::safety {

// Default spawn-oriented fork context.  The explicit 16 MiB budget routes
// through the cache-tier rule as a DRAM-sized background workload; callers
// that want sequential degradation pass a smaller ctx such as
// effects::BgDrainCtx explicitly.
using PermissionForkSpawnCtx = ::crucible::effects::ExecCtx<
    ::crucible::effects::Bg,
    ::crucible::effects::ctx_numa::Local,
    ::crucible::effects::ctx_alloc::Arena,
    ::crucible::effects::ctx_heat::Cold,
    ::crucible::effects::ctx_resid::DRAM,
    ::crucible::effects::Row<
        ::crucible::effects::Effect::Bg,
        ::crucible::effects::Effect::Alloc>,
    ::crucible::effects::ctx_workload::ByteBudget<16ULL * 1024ULL * 1024ULL>>;

template <typename Ctx, typename Parent, typename... Children>
concept CtxFitsPermissionFork =
    ::crucible::effects::IsExecCtx<Ctx>
    && ::crucible::effects::CtxOwnsCapability<Ctx, ::crucible::effects::Effect::Bg>
    && CtxAdmitsPermission<Parent, Ctx>
    && (CtxAdmitsPermission<Children, Ctx> && ...)
    && splits_into_pack_v<Parent, Children...>
    && splits_into_pack_authoring_witness_v<Parent, Children...>;

namespace detail {

template <typename Callable, typename Child, typename Ctx>
concept PermissionForkCtxCallable =
    std::is_invocable_v<Callable, Permission<Child>, Ctx const&>
    && std::is_nothrow_invocable_v<Callable, Permission<Child>, Ctx const&>;

template <typename Ctx, typename ChildrenTuple, typename CallablesTuple>
struct permission_fork_ctx_callables;

template <typename Ctx, typename... Children, typename... Callables>
struct permission_fork_ctx_callables<
    Ctx,
    std::tuple<Children...>,
    std::tuple<Callables...>> {
    static consteval bool value() noexcept {
        if constexpr (sizeof...(Children) != sizeof...(Callables)) {
            return false;
        } else {
            return (PermissionForkCtxCallable<Callables, Children, Ctx> && ...);
        }
    }
};

template <typename Ctx, typename ChildrenTuple, typename CallablesTuple>
inline constexpr bool permission_fork_ctx_callables_v =
    permission_fork_ctx_callables<Ctx, ChildrenTuple, CallablesTuple>::value();

template <typename Ctx>
[[nodiscard]] consteval bool permission_fork_zero_budget_v() noexcept {
    constexpr auto budget = ::crucible::concurrent::ctx_workbudget<Ctx>();
    return budget.read_bytes == 0 && budget.write_bytes == 0;
}

// Spawn one jthread per child, capturing the child's Permission and
// the corresponding callable/context by move.  std::array<jthread, N>'s
// destructor (in fn epilogue) joins all threads in reverse order.
//
// Implemented as a fold over std::index_sequence so we can handle
// arbitrary N at compile time without recursion.
//
// Noexcept contract: std::jthread's ctor is NOT noexcept (it wraps
// pthread_create, which can fail with EAGAIN/ENOMEM/EINVAL).  The
// brace-init below is wrapped in try / catch (...) -> std::abort()
// so the noexcept claim holds at the source level regardless of
// whether the build mode is -fno-exceptions (libstdc++ pre-rewrites
// the throw) or -fexceptions (our catch fires).  See the top-of-
// file doc block for the full rationale (fixy-A1-010).

template <typename Ctx, typename Children, typename Callables, std::size_t... Is>
void permission_fork_spawn_(Ctx const& ctx,
                             Children&& children,
                             Callables&& callables,
                             std::index_sequence<Is...>) noexcept
{
    // Build the array of jthreads in-place.  Each entry's lambda
    // captures-by-move its child Permission and its corresponding
    // callable.  jthread's stop_token argument is unused in our
    // pattern (the body runs to completion without cancellation).
    //
    // The array's destructor joins all jthreads when the function
    // returns, by reverse-of-construction order (which doesn't matter
    // for correctness — all bodies must complete before we return).
    //
    // fixy-A1-010: `std::jthread`'s ctor is not noexcept (pthread_
    // create can fail with EAGAIN/ENOMEM/EINVAL).  Under
    // `-fexceptions` that throws `std::system_error` past our
    // noexcept boundary into `std::terminate`; under
    // `-fno-exceptions` libstdc++ pre-rewrites the throw to
    // `_GLIBCXX_THROW_OR_ABORT` → `std::abort`.  We make the
    // failure mode source-level deterministic by wrapping the
    // brace-init in a `try`/`catch (...)` that converts ANY
    // exception to `std::abort`, matching Crucible's
    // resource-failure-aborts policy on every build mode.
#if defined(__cpp_exceptions)
    try {
#endif
        [[maybe_unused]] std::array<std::jthread, sizeof...(Is)> threads = {
            std::jthread{
                [child_perm = std::move(std::get<Is>(std::forward<Children>(children))),
                 callable   = std::move(std::get<Is>(std::forward<Callables>(callables))),
                 child_ctx  = ctx]
                (std::stop_token) mutable noexcept {
                    callable(std::move(child_perm), child_ctx);
                }
            }...
        };
        // ~std::array runs here, joining each jthread.
#if defined(__cpp_exceptions)
    } catch (...) {
        // pthread_create failed (EAGAIN / ENOMEM / EINVAL).  The
        // catch-all is intentional: under future libstdc++ versions
        // or non-libstdc++ runtimes the exception type may change;
        // the contract is "any failure from std::jthread construction
        // becomes std::abort", not specifically "std::system_error".
        std::abort();
    }
#endif
}

template <typename Ctx, typename Children, typename Callables, std::size_t... Is>
void permission_fork_inline_(Ctx const& ctx,
                             Children&& children,
                             Callables&& callables,
                             std::index_sequence<Is...>) noexcept
{
    (std::get<Is>(std::forward<Callables>(callables))(
         std::move(std::get<Is>(std::forward<Children>(children))),
         ctx), ...);
}

}  // namespace detail

// ── mint_permission_fork<Children...>(ctx, parent, callables...) ──────────
//
// Encodes CSL's parallel composition rule.  Returns the rebuilt
// parent Permission after all children have joined.
//
// Constraints:
//   * CtxFitsPermissionFork<Ctx, Parent, Children...>
//   * splits_into_pack_v<Parent, Children...>
//   * sizeof...(Children) == sizeof...(Callables)
//   * each Callable_i invocable as
//     Callable_i(Permission<Child_i>&&, Ctx const&)

template <typename... Children, typename Ctx, typename Parent, typename... Callables>
    requires CtxFitsPermissionFork<Ctx, Parent, Children...>
          && detail::permission_fork_ctx_callables_v<
              Ctx,
              std::tuple<Children...>,
              std::tuple<Callables...>>
[[nodiscard]] Permission<Parent> mint_permission_fork(
    Ctx const& ctx,
    Permission<Parent>&& parent,
    Callables&&... callables) noexcept
{
    static_assert(sizeof...(Children) == sizeof...(Callables),
        "mint_permission_fork: number of Child tags must match number of callables.");
    static_assert((std::is_invocable_v<
                       Callables,
                       Permission<Children>,
                       Ctx const&> && ...),
        "mint_permission_fork: each callable must be invocable as "
        "Callable_i(Permission<Child_i>&&, Ctx const&).");
    static_assert((std::is_nothrow_invocable_v<
                       Callables,
                       Permission<Children>,
                       Ctx const&> && ...),
        "mint_permission_fork: callables must be noexcept (Crucible -fno-exceptions).");

    // FIXY-V-087: reject crucible::fixy::ctrl::throws anywhere in a
    // Callable's TYPE TREE.  `is_nothrow_invocable_v` above catches
    // SYNTACTIC lying-by-noexcept-decl; this stronger TYPE-LEVEL gate
    // catches NAMED callable wrappers that carry an explicit throws
    // grant via template-argument annotation (e.g. a `fixy::fn<T,
    // ..., ctrl::throws, ...>` aggregator).  Lambda closures and
    // non-template carriers naturally pass (no introspectable type
    // tree) — that is OK; the noexcept-invocable rail catches them.
    //
    // The structured-concurrency fork-join (jthread spawn + RAII
    // join, see `permission_fork_spawn_`) cannot tolerate a throw
    // tearing through it: parent Permission rebuild would be skipped
    // and child Permission lifetimes would be stranded with no holder,
    // breaking the linear-resource invariant on the parent region.
    // See `crucible/fixy/ctrl/Throws.h` for the tag and the
    // type-tree-contains trait + cv-ref decay discipline.
    static_assert(!(::crucible::fixy::ctrl::type_tree_contains_throws_v<
                        std::decay_t<Callables>> || ...),
        "mint_permission_fork: Callables may not carry the "
        "crucible::fixy::ctrl::throws grant — exceptions tearing "
        "through structured fork-join would corrupt parent Permission "
        "rebuild and child Permission lifetimes (FIXY-V-087).");

    // Step 1: split the parent into disjoint child Permissions.
    auto child_perms =
        mint_permission_split_n<Children...>(ctx, std::move(parent));

    // Step 2: pack the callables for index_sequence-driven spawn.
    auto callable_pack = std::tuple<std::decay_t<Callables>...>{
        std::forward<Callables>(callables)...
    };

    // Step 3: obey the cache-tier rule.  A compile-time zero working set
    // is necessarily inline, so avoid even the Topology singleton read.
    // Nonzero budgets route through ParallelismRule because cache sizes
    // and cgroup CPU limits are host facts.
    if constexpr (detail::permission_fork_zero_budget_v<Ctx>()) {
        detail::permission_fork_inline_(
            ctx,
            std::move(child_perms),
            std::move(callable_pack),
            std::index_sequence_for<Children...>{});
    } else {
        const auto decision =
            ::crucible::concurrent::parallelism_decision_for<Ctx>();
        if (decision.kind
            == ::crucible::concurrent::ParallelismDecision::Kind::Sequential) {
            detail::permission_fork_inline_(
                ctx,
                std::move(child_perms),
                std::move(callable_pack),
                std::index_sequence_for<Children...>{});
        } else {
            detail::permission_fork_spawn_(
                ctx,
                std::move(child_perms),
                std::move(callable_pack),
                std::index_sequence_for<Children...>{});
        }
    }

    // Step 4: rebuild the parent Permission.  All children consumed
    // their tokens; no holder remains; the parent region is again
    // exclusive in the caller's scope.  See discipline note in
    // safety/Permission.h about why this is sound under the
    // linearity/no-exceptions invariants.
    //
    // fixy-A1-009: rebuild is now passkey-gated.  The helper
    // `detail::rebuild_parent_after_fork_<Parent>()` is the single
    // public entrypoint to a fresh `Permission<Parent>` after
    // structured-join — the previously-public free function
    // `permission_fork_rebuild_` was removed.
    return detail::rebuild_parent_after_fork_<Parent>();
}

}  // namespace crucible::safety
