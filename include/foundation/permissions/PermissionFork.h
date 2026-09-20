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
// The two arms share one body and one set of checks; the arm is a bool
// parameter of both, and the checks' diagnostics name the arm they
// fire for.  The old header repeated the four static_asserts and the
// split-pack-run-rebuild sequence once per arm.
//
// One thread per child, spawned and joined inside the call.  That suits
// a few children with long bodies.  It is the wrong shape for many short
// tasks, which want a work-stealing pool instead.
//
// A callable's own noexcept specification is all that is checked here.
// The old header also rejected a callable whose type carried the
// crucible::fixy::ctrl::throws grant; that name is above this layer, so
// the structural check belongs to fixy/os/Spawn.h.
//
// Old spelling: include/crucible/permissions/PermissionFork.h, namespace
// crucible::safety.

#include <foundation/Platform.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <array>
#include <cstddef>
#include <cstdlib>
#include <string_view>
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

// A static_assert message assembled at compile time, so one check can
// name the arm it fires for.  The buffer is sized for the longest
// message below; the text past the terminator is never read.
struct fork_diagnostic {
    std::array<char, 256> text{};
    std::size_t length = 0;

    consteval fork_diagnostic(bool spawn, std::string_view rest) noexcept {
        append(spawn ? "mint_permission_fork" : "mint_permission_fork_inline");
        append(rest);
    }
    consteval void append(std::string_view part) noexcept {
        for (char c : part) {
            if (length < text.size()) text[length++] = c;
        }
    }
    [[nodiscard]] consteval std::size_t size() const noexcept { return length; }
    [[nodiscard]] consteval char const* data() const noexcept { return text.data(); }
};

// The four checks both arms make.  The split this delegates to asserts
// the pairwise-distinct one again; asserting it here makes the
// diagnostic name the fork.
template <bool Spawn, typename Ctx, typename ChildrenTuple, typename CallablesTuple>
consteval void permission_fork_check_() noexcept {
    []<typename... Children, typename... Callables>(std::tuple<Children...>*, std::tuple<Callables...>*) {
        static_assert(sizeof...(Children) == sizeof...(Callables),
                      fork_diagnostic(Spawn, ": number of Child tags must match number of callables."));
        static_assert(all_distinct_tags_v<Children...>,
                      fork_diagnostic(Spawn, Spawn ? ": Child region tags must be PAIRWISE DISTINCT — "
                                                     "forking two threads with Permission<A> each would alias "
                                                     "region A and produce a data race."
                                                   : ": Child region tags must be PAIRWISE DISTINCT — "
                                                     "two bodies with Permission<A> each would alias region A."));
        static_assert((std::is_invocable_v<Callables, Permission<Children>, Ctx const&> && ...),
                      fork_diagnostic(Spawn, ": each callable must be invocable as "
                                             "Callable_i(Permission<Child_i>&&, Ctx const&)."));
        static_assert((std::is_nothrow_invocable_v<Callables, Permission<Children>, Ctx const&> && ...),
                      fork_diagnostic(Spawn, ": callables must be noexcept."));
    }(static_cast<ChildrenTuple*>(nullptr), static_cast<CallablesTuple*>(nullptr));
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
constexpr void permission_fork_inline_(Ctx const& ctx, Children&& children, Callables&& callables,
                                       std::index_sequence<Is...>) noexcept {
    (std::get<Is>(std::forward<Callables>(callables))(std::move(std::get<Is>(std::forward<Children>(children))), ctx),
     ...);
}

// The one body: split the parent, pack the callables, run them on the
// arm the caller chose, and rebuild the parent once every body has
// finished.
template <bool Spawn, typename... Children, typename Ctx, typename Parent, typename... Callables>
constexpr Permission<Parent> permission_fork_(Ctx const& ctx, Permission<Parent>&& parent,
                                              Callables&&... callables) noexcept {
    permission_fork_check_<Spawn, Ctx, std::tuple<Children...>, std::tuple<Callables...>>();

    auto child_perms = mint_permission_split_n<Children...>(ctx, std::move(parent));

    auto callable_pack = std::tuple<std::decay_t<Callables>...>{std::forward<Callables>(callables)...};

    if constexpr (Spawn) {
        permission_fork_spawn_(ctx, std::move(child_perms), std::move(callable_pack),
                               std::index_sequence_for<Children...>{});
    } else {
        permission_fork_inline_(ctx, std::move(child_perms), std::move(callable_pack),
                                std::index_sequence_for<Children...>{});
    }

    // This scope is the sole friend of ForkRebuildKey, so this is the
    // only place the key can be built.  The proof that the reissue is
    // legitimate is that `parent` was taken by rvalue and consumed at
    // the split above.  Do not factor this call out into a helper that
    // does not consume a Permission<Parent> — that is exactly the shape
    // which once made the parent forgeable from any translation unit.
    return ForkRebuildAccess::rebuild<Parent>(ForkRebuildKey{});
}

}  // namespace detail

template <typename... Children, typename Ctx, typename Parent, typename... Callables>
    requires CtxFitsPermissionFork<Ctx, Parent, Children...>
          && detail::permission_fork_ctx_callables_v<Ctx, std::tuple<Children...>, std::tuple<Callables...>>
[[nodiscard]] Permission<Parent> mint_permission_fork(Ctx const& ctx, Permission<Parent>&& parent,
                                                      Callables&&... callables) noexcept {
    return detail::permission_fork_<true, Children...>(ctx, std::move(parent), std::forward<Callables>(callables)...);
}

// The bodies run one after another on the calling thread, in child
// order.  The split and the rebuild are the same as the spawning arm's,
// so a body still holds its own child token and nothing else.
template <typename... Children, typename Ctx, typename Parent, typename... Callables>
    requires CtxFitsPermissionForkInline<Ctx, Parent, Children...>
          && detail::permission_fork_ctx_callables_v<Ctx, std::tuple<Children...>, std::tuple<Callables...>>
[[nodiscard]] constexpr Permission<Parent> mint_permission_fork_inline(Ctx const& ctx, Permission<Parent>&& parent,
                                                                       Callables&&... callables) noexcept {
    return detail::permission_fork_<false, Children...>(ctx, std::move(parent), std::forward<Callables>(callables)...);
}

}  // namespace foundation::permissions
