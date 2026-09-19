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
// This layer carries no cost model, so it cannot decide whether the
// bodies run on their own threads or inline in child order.  It offers
// the two arms as two mints and the layer above chooses.  The spawning
// arm requires a context that owns the background effect; the inline
// arm requires no effect beyond what the tags themselves need.  The old
// header made the choice itself from the context's workload budget,
// which foundation's ExecCtx does not carry.
//
// One thread per child, spawned and joined inside the call.  That suits
// a few children with long bodies.  It is the wrong shape for many short
// tasks, which want a work-stealing pool instead.
//
// A callable's own noexcept specification is all that is checked here.
// The old header also rejected a callable whose type carried the
// crucible::fixy::ctrl::throws grant; that name is above this layer, so
// the structural check moves to fixy/os/Spawn.h (task A13.4).
//
// Old spelling: include/crucible/permissions/PermissionFork.h, namespace
// crucible::safety.

#include <foundation/Platform.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <array>
#include <cstddef>
#include <cstdlib>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>

namespace foundation::permissions {

template <typename Ctx, typename Parent, typename... Children>
concept CtxFitsPermissionForkInline =
    ::foundation::effects::IsExecCtx<Ctx> && CtxAdmitsPermission<Parent, Ctx>
    && (CtxAdmitsPermission<Children, Ctx> && ...)
    && splits_into_pack_v<Parent, Children...> && splits_into_pack_authoring_witness_v<Parent, Children...>;

template <typename Ctx, typename Parent, typename... Children>
concept CtxFitsPermissionFork = CtxFitsPermissionForkInline<Ctx, Parent, Children...>
                             && ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Bg>;

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
constexpr void permission_fork_inline_(Ctx const& ctx, Children&& children, Callables&& callables,
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

    auto child_perms = mint_permission_split_n<Children...>(ctx, std::move(parent));

    auto callable_pack = std::tuple<std::decay_t<Callables>...>{std::forward<Callables>(callables)...};

    detail::permission_fork_spawn_(ctx, std::move(child_perms), std::move(callable_pack),
                                   std::index_sequence_for<Children...>{});

    return detail::rebuild_parent_after_fork_<Parent>();
}

// The bodies run one after another on the calling thread, in child
// order.  The split and the rebuild are the same as the spawning arm's,
// so a body still holds its own child token and nothing else.
template <typename... Children, typename Ctx, typename Parent, typename... Callables>
    requires CtxFitsPermissionForkInline<Ctx, Parent, Children...>
          && detail::permission_fork_ctx_callables_v<Ctx, std::tuple<Children...>, std::tuple<Callables...>>
[[nodiscard]] constexpr Permission<Parent> mint_permission_fork_inline(Ctx const& ctx, Permission<Parent>&& parent,
                                                                       Callables&&... callables) noexcept {
    static_assert(sizeof...(Children) == sizeof...(Callables),
                  "mint_permission_fork_inline: number of Child tags must match number of callables.");
    static_assert(all_distinct_tags_v<Children...>,
                  "mint_permission_fork_inline: Child region tags must be PAIRWISE DISTINCT — "
                  "two bodies with Permission<A> each would alias region A.");
    static_assert((std::is_invocable_v<Callables, Permission<Children>, Ctx const&> && ...),
                  "mint_permission_fork_inline: each callable must be invocable as "
                  "Callable_i(Permission<Child_i>&&, Ctx const&).");
    static_assert((std::is_nothrow_invocable_v<Callables, Permission<Children>, Ctx const&> && ...),
                  "mint_permission_fork_inline: callables must be noexcept.");

    auto child_perms = mint_permission_split_n<Children...>(ctx, std::move(parent));

    auto callable_pack = std::tuple<std::decay_t<Callables>...>{std::forward<Callables>(callables)...};

    detail::permission_fork_inline_(ctx, std::move(child_perms), std::move(callable_pack),
                                    std::index_sequence_for<Children...>{});

    return detail::rebuild_parent_after_fork_<Parent>();
}

}  // namespace foundation::permissions
