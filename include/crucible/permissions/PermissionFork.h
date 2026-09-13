#pragma once

// Encodes the parallel composition rule of concurrent separation logic:
//
//   {P1} C1 {Q1}   ...   {Pn} Cn {Qn}
//   ─────────────────────────────────────────────────
//   {P1 * ... * Pn} (C1 || ... || Cn) {Q1 * ... * Qn}
//
// The parent permission splits into disjoint children, one per body.
// Each body consumes its own child token, and the parent comes back only
// once every body has finished.  Disjointness is settled when the split
// manifest is checked, so no two bodies can reach the same region.
//
// Whether the bodies run on their own threads or inline in child order
// is decided from the context's workload budget.  The context carries
// the background effect either way, because the caller cannot know which
// arm it gets.
//
// One thread per child, spawned and joined inside the call.  That suits
// a few children with long bodies.  It is the wrong shape for many short
// tasks, which want a work-stealing pool instead.

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

namespace crucible::safety {

// The budget is large enough that the cache-tier rule reads this as a
// DRAM-sized background workload and picks the spawning arm.  A caller
// that wants the inline arm passes a context with a smaller budget.
using PermissionForkSpawnCtx = ::crucible::effects::ExecCtx<
    ::crucible::effects::Bg, ::crucible::effects::ctx_numa::Local, ::crucible::effects::ctx_alloc::Arena,
    ::crucible::effects::ctx_heat::Cold, ::crucible::effects::ctx_resid::DRAM,
    ::crucible::effects::Row<::crucible::effects::Effect::Bg, ::crucible::effects::Effect::Alloc>,
    ::crucible::effects::ctx_workload::ByteBudget<16ULL * 1024ULL * 1024ULL>>;

template <typename Ctx, typename Parent, typename... Children>
concept CtxFitsPermissionFork =
    ::crucible::effects::IsExecCtx<Ctx> && ::crucible::effects::CtxOwnsCapability<Ctx, ::crucible::effects::Effect::Bg>
    && CtxAdmitsPermission<Parent, Ctx> && (CtxAdmitsPermission<Children, Ctx> && ...)
    && splits_into_pack_v<Parent, Children...> && splits_into_pack_authoring_witness_v<Parent, Children...>;

namespace detail {

template <typename Callable, typename Child, typename Ctx>
concept PermissionForkCtxCallable = std::is_invocable_v<Callable, Permission<Child>, Ctx const&>
                                 && std::is_nothrow_invocable_v<Callable, Permission<Child>, Ctx const&>;

template <typename Ctx, typename ChildrenTuple, typename CallablesTuple>
struct permission_fork_ctx_callables;

template <typename Ctx, typename... Children, typename... Callables>
struct permission_fork_ctx_callables<Ctx, std::tuple<Children...>, std::tuple<Callables...>> {
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

template <typename Ctx, typename Children, typename Callables, std::size_t... Is>
void permission_fork_spawn_(Ctx const& ctx, Children&& children, Callables&& callables,
                            std::index_sequence<Is...>) noexcept {
    // A jthread constructor is not noexcept, because the thread creation
    // under it can fail on resource exhaustion.  Without the catch, that
    // failure reaches this function's noexcept boundary and terminates
    // when the build has exceptions on, but aborts when it does not.  The
    // catch makes both builds abort, which is what a resource failure does
    // everywhere else here.
#if defined(__cpp_exceptions)
    try {
#endif
        [[maybe_unused]] std::array<std::jthread, sizeof...(Is)> threads = {std::jthread{
            [child_perm = std::move(std::get<Is>(std::forward<Children>(children))),
             callable = std::move(std::get<Is>(std::forward<Callables>(callables))),
             child_ctx = ctx](std::stop_token) mutable noexcept { callable(std::move(child_perm), child_ctx); }}...};
#if defined(__cpp_exceptions)
    } catch (...) {
        // The catch is deliberately untyped.  The contract is that any
        // failure to construct a thread aborts, and the exception type a
        // given standard library reports it with is not fixed.
        std::abort();
    }
#endif
}

template <typename Ctx, typename Children, typename Callables, std::size_t... Is>
void permission_fork_inline_(Ctx const& ctx, Children&& children, Callables&& callables,
                             std::index_sequence<Is...>) noexcept {
    (std::get<Is>(std::forward<Callables>(callables))(std::move(std::get<Is>(std::forward<Children>(children))), ctx),
     ...);
}

}  // namespace detail

template <typename... Children, typename Ctx, typename Parent, typename... Callables>
    requires CtxFitsPermissionFork<Ctx, Parent, Children...>
          && detail::permission_fork_ctx_callables_v<Ctx, std::tuple<Children...>, std::tuple<Callables...>>
[[nodiscard]] Permission<Parent> mint_permission_fork(Ctx const& ctx, Permission<Parent>&& parent,
                                                      Callables&&... callables) noexcept {
    static_assert(sizeof...(Children) == sizeof...(Callables),
                  "mint_permission_fork: number of Child tags must match number of callables.");
    // The split this delegates to asserts the same thing.  Asserting it
    // again here makes the diagnostic name the fork.
    static_assert(all_distinct_tags_v<Children...>,
                  "mint_permission_fork: Child region tags must be PAIRWISE DISTINCT — "
                  "forking two threads with Permission<A> each would alias region A and "
                  "produce a data race.");
    static_assert((std::is_invocable_v<Callables, Permission<Children>, Ctx const&> && ...),
                  "mint_permission_fork: each callable must be invocable as "
                  "Callable_i(Permission<Child_i>&&, Ctx const&).");
    static_assert((std::is_nothrow_invocable_v<Callables, Permission<Children>, Ctx const&> && ...),
                  "mint_permission_fork: callables must be noexcept.");

    // The nothrow-invocable check above reads the callable's own noexcept
    // specification.  A named wrapper can satisfy it while still carrying
    // an explicit grant to throw in its template arguments, so the grant
    // is rejected structurally as well.  A lambda has no such type tree to
    // inspect and passes here, which is fine: the check above covers it.
    static_assert(!(::crucible::fixy::ctrl::type_tree_contains_throws_v<std::decay_t<Callables>> || ...),
                  "mint_permission_fork: Callables may not carry the "
                  "crucible::fixy::ctrl::throws grant — exceptions tearing "
                  "through structured fork-join would corrupt parent Permission "
                  "rebuild and child Permission lifetimes.");

    auto child_perms = mint_permission_split_n<Children...>(ctx, std::move(parent));

    auto callable_pack = std::tuple<std::decay_t<Callables>...>{std::forward<Callables>(callables)...};

    // A working set that is zero at compile time can only be inline, so
    // that case skips the host probe entirely.  Any other budget has to
    // ask, because cache sizes and CPU limits are facts about the host.
    if constexpr (detail::permission_fork_zero_budget_v<Ctx>()) {
        detail::permission_fork_inline_(ctx, std::move(child_perms), std::move(callable_pack),
                                        std::index_sequence_for<Children...>{});
    } else {
        const auto decision = ::crucible::concurrent::parallelism_decision_for<Ctx>();
        if (decision.kind == ::crucible::concurrent::ParallelismDecision::Kind::Sequential) {
            detail::permission_fork_inline_(ctx, std::move(child_perms), std::move(callable_pack),
                                            std::index_sequence_for<Children...>{});
        } else {
            detail::permission_fork_spawn_(ctx, std::move(child_perms), std::move(callable_pack),
                                           std::index_sequence_for<Children...>{});
        }
    }

    return detail::rebuild_parent_after_fork_<Parent>();
}

}  // namespace crucible::safety
