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
// split-pack-run-rebuild sequence once per arm.  The body is a private
// member of PermissionForkRunner, whose only friends are the two mints.
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
    && can_split_into_pack_v<Parent, Children...> && has_split_pack_authoring_witness_v<Parent, Children...>;

template <typename Ctx, typename Parent, typename... Children>
concept CtxFitsPermissionFork = CtxFitsPermissionForkInline<Ctx, Parent, Children...>
                             && ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Bg>;

namespace detail {

template <typename Callable, typename Child, typename Ctx>
concept PermissionForkCtxCallable = std::is_invocable_v<Callable, Permission<Child>, Ctx const&>
                                 && std::is_nothrow_invocable_v<Callable, Permission<Child>, Ctx const&>;

// Each body takes the token of its own child and the context, without
// throwing.  A pack of bodies whose length is not the length of the
// children, or a shape that is not two tuples, takes nothing.
template <typename Ctx, typename ChildrenTuple, typename CallablesTuple>
struct can_each_body_take_its_child : std::false_type {};

template <typename Ctx, typename... Children, typename... Callables>
    requires(sizeof...(Children) == sizeof...(Callables))
struct can_each_body_take_its_child<Ctx, std::tuple<Children...>, std::tuple<Callables...>>
    : std::bool_constant<(PermissionForkCtxCallable<Callables, Children, Ctx> && ...)> {};

template <typename Ctx, typename ChildrenTuple, typename CallablesTuple>
inline constexpr bool can_each_body_take_its_child_v =
    can_each_body_take_its_child<Ctx, ChildrenTuple, CallablesTuple>::value;

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
// diagnostic name the fork.  The constraint on the runner's body refuses
// a count mismatch and a body that cannot take its child first, so of
// the four only the pairwise-distinct check can fire.
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

// The fit of one arm: the spawning arm needs the background effect, the
// inline arm does not.  The runner below states it again on its own body,
// so the check stands where the threads start and not only at the doors.
template <bool Spawn, typename Ctx, typename Parent, typename... Children>
concept CtxFitsPermissionForkArm = (Spawn && CtxFitsPermissionFork<Ctx, Parent, Children...>)
                                || (!Spawn && CtxFitsPermissionForkInline<Ctx, Parent, Children...>);

}  // namespace detail

// The two mints, declared before the runner so that its friend
// declarations name them and nothing else.
template <typename... Children, typename Ctx, typename Parent, typename Brand, typename... Callables>
    requires CtxFitsPermissionFork<Ctx, Parent, Children...>
          && detail::can_each_body_take_its_child_v<Ctx, std::tuple<Children...>, std::tuple<Callables...>>
[[nodiscard]] Permission<Parent, Brand> mint_permission_fork(Ctx const& ctx, Permission<Parent, Brand>&& parent,
                                                             Callables&&... callables) noexcept;

template <typename... Children, typename Ctx, typename Parent, typename Brand, typename... Callables>
    requires CtxFitsPermissionForkInline<Ctx, Parent, Children...>
          && detail::can_each_body_take_its_child_v<Ctx, std::tuple<Children...>, std::tuple<Callables...>>
[[nodiscard]] constexpr Permission<Parent, Brand>
mint_permission_fork_inline(Ctx const& ctx, Permission<Parent, Brand>&& parent, Callables&&... callables) noexcept;

// The body of a fork: split the parent, start or run one body per child,
// and rebuild the parent once every body has finished.  It does the work
// of the two mints, so only the two mints may reach it.  Every member is
// private and static, the two mints are the only friends, and the class
// is final and cannot be built, so no other scope can call a member,
// derive to reach one, or take one's address.
//
// A free function template in a detail namespace once held this body.
// Any translation unit could call it, and it carried no constraint, so a
// caller under the foreground context started threads the spawning mint
// would have refused for want of the background effect.
//
// The friend declarations repeat each mint's constraint exactly, because
// the language requires it.  The class sits in this namespace, not in
// detail, for the reason Permission.h gives for perm_mint_key: a friend
// declaration in a nested namespace would declare a new function there
// and befriend that one instead.
class PermissionForkRunner final {
    // No object of the runner exists.  Every constructor is deleted and
    // the destructor is user-provided, so the class is neither trivially
    // copyable nor an implicit-lifetime type, and no byte route
    // (std::bit_cast, std::start_lifetime_as) can make one either.
    PermissionForkRunner() = delete("the fork runner holds static members only; no object of it exists");
    PermissionForkRunner(const PermissionForkRunner&) = delete("the fork runner holds static members only");
    PermissionForkRunner& operator=(const PermissionForkRunner&) = delete("the fork runner holds static members only");
    PermissionForkRunner(PermissionForkRunner&&) = delete("the fork runner holds static members only");
    PermissionForkRunner& operator=(PermissionForkRunner&&) = delete("the fork runner holds static members only");
    constexpr ~PermissionForkRunner() noexcept {}

    template <typename... Children, typename Ctx, typename Parent, typename Brand, typename... Callables>
        requires CtxFitsPermissionFork<Ctx, Parent, Children...>
              && detail::can_each_body_take_its_child_v<Ctx, std::tuple<Children...>, std::tuple<Callables...>>
    friend Permission<Parent, Brand> mint_permission_fork(Ctx const& ctx, Permission<Parent, Brand>&& parent,
                                                          Callables&&... callables) noexcept;

    template <typename... Children, typename Ctx, typename Parent, typename Brand, typename... Callables>
        requires CtxFitsPermissionForkInline<Ctx, Parent, Children...>
              && detail::can_each_body_take_its_child_v<Ctx, std::tuple<Children...>, std::tuple<Callables...>>
    friend constexpr Permission<Parent, Brand> mint_permission_fork_inline(Ctx const& ctx,
                                                                           Permission<Parent, Brand>&& parent,
                                                                           Callables&&... callables) noexcept;

    template <typename Ctx, typename Children, typename Callables, std::size_t... Is>
    static void spawn_(Ctx const& ctx, Children&& children, Callables&& callables,
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
    static constexpr void run_inline_(Ctx const& ctx, Children&& children, Callables&& callables,
                                      std::index_sequence<Is...>) noexcept {
        (std::get<Is>(std::forward<Callables>(callables))(std::move(std::get<Is>(std::forward<Children>(children))),
                                                          ctx),
         ...);
    }

    // The children carry the parent's brand into the bodies, and the
    // rebuilt parent carries it back out, so the region that returns is
    // the one that went in.
    template <bool Spawn, typename... Children, typename Ctx, typename Parent, typename Brand, typename... Callables>
        requires detail::CtxFitsPermissionForkArm<Spawn, Ctx, Parent, Children...>
              && detail::can_each_body_take_its_child_v<Ctx, std::tuple<Children...>, std::tuple<Callables...>>
    static constexpr Permission<Parent, Brand> run_(Ctx const& ctx, Permission<Parent, Brand>&& parent,
                                                    Callables&&... callables) noexcept {
        detail::permission_fork_check_<Spawn, Ctx, std::tuple<Children...>, std::tuple<Callables...>>();

        auto child_perms = mint_permission_split_n<Children...>(ctx, std::move(parent));

        auto callable_pack = std::tuple<std::decay_t<Callables>...>{std::forward<Callables>(callables)...};

        if constexpr (Spawn) {
            spawn_(ctx, std::move(child_perms), std::move(callable_pack), std::index_sequence_for<Children...>{});
        } else {
            run_inline_(ctx, std::move(child_perms), std::move(callable_pack), std::index_sequence_for<Children...>{});
        }

        // This class is the sole friend of ForkRebuildKey, so this is the
        // only place the key can be built.  The proof that the reissue is
        // legitimate is that `parent` was taken by rvalue and consumed at
        // the split above.  Do not factor this call out into a member that
        // does not consume a Permission<Parent>: that is exactly the shape
        // which once made the parent forgeable from any translation unit.
        return detail::ForkRebuildAccess::rebuild<Parent, Brand>(detail::ForkRebuildKey{});
    }
};

// The clause names two gates, and folding them behind one name to read as
// a single concept was measured and reverted.  The children half then
// goes through a variable template over a tuple of the child tags, which
// is one atomic constraint, so the compiler stops reporting there: the
// two fixtures on this mint lost the conjunct that answered false — the
// absent background capability, the pack that does not partition — and
// named a tuple wrapper instead.  Both halves stay spelled here.
template <typename... Children, typename Ctx, typename Parent, typename Brand, typename... Callables>
    requires CtxFitsPermissionFork<Ctx, Parent, Children...>
          && detail::can_each_body_take_its_child_v<Ctx, std::tuple<Children...>, std::tuple<Callables...>>
[[nodiscard]] Permission<Parent, Brand> mint_permission_fork(Ctx const& ctx, Permission<Parent, Brand>&& parent,
                                                             Callables&&... callables) noexcept {
    return PermissionForkRunner::run_<true, Children...>(ctx, std::move(parent), std::forward<Callables>(callables)...);
}

// The bodies run one after another on the calling thread, in child
// order.  The split and the rebuild are the same as the spawning arm's,
// so a body still holds its own child token and nothing else.
// The inline arm spells its two halves for the same reason as the arm
// above, and differs from it in one conjunct: it demands no background
// capability, because it starts no thread.
template <typename... Children, typename Ctx, typename Parent, typename Brand, typename... Callables>
    requires CtxFitsPermissionForkInline<Ctx, Parent, Children...>
          && detail::can_each_body_take_its_child_v<Ctx, std::tuple<Children...>, std::tuple<Callables...>>
[[nodiscard]] constexpr Permission<Parent, Brand>
mint_permission_fork_inline(Ctx const& ctx, Permission<Parent, Brand>&& parent, Callables&&... callables) noexcept {
    return PermissionForkRunner::run_<false, Children...>(ctx, std::move(parent),
                                                          std::forward<Callables>(callables)...);
}

}  // namespace foundation::permissions
