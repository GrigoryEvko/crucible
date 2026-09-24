#pragma once

// The context-bound entry points of a session that starts with no
// permission.  mint_session gives the first handle to the caller.
// with_session gives the first handle to a body, and gives the Resource
// back when the body brings the session to End.
//
// ── The gate ────────────────────────────────────────────────────────
//
// CtxFitsSession is CtxFitsSessionFrom of fixy/session/Handle.h with no
// tag, so it is the gate of mint_permissioned_session at the empty set.
// The two mints cannot drift apart, because they read one concept.  With
// no tag, the context part of the gate asks only that the context is an
// execution context: no tag brings a permission row for the context to
// admit.
//
// The session core has no gate on the effect row of a payload.  A
// context that holds no Alloc capability can mint a session whose
// messages carry an Alloc computation.  The old tree checked each payload
// row against the row of the context; neither mint here does.
//
// ── A channel handle as the Resource ────────────────────────────────
//
// A handle of a permissioned channel is move-only, so it is an owned
// SessionResource.  with_session takes the handle by value, keeps it in
// the session for the whole body, and returns it when the body closes the
// session.  This is a loan by move: while the body runs, the caller holds
// only a moved-from handle, which reaches no channel.  So the session and
// the caller never act on the channel at the same time, and the session
// never holds a pointer that the channel handle can outlive or move away
// from.  A raw pointer to the channel handle stays refused by
// SessionResource.
//
// The body must return a handle at End, and only a handle with the brand
// of this call, so the session cannot outlive the body (Thiemann, ICFP
// 2023).  A body that returns early, keeps the handle somewhere else, or
// returns the End handle of another session does not compile.
//
// A loop protocol reaches End only through a branch that ends it, for
// example Loop<Select<Send<T, Continue>, End>>.  A body that streams takes
// the first branch for each message and the last branch to stop.
//
// Complexity: linear in the size of the protocol, at compile time.  At
// run time each step moves the Resource once.

#include <fixy/session/Handle.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/PermSet.h>

#include <functional>
#include <source_location>
#include <type_traits>
#include <utility>

namespace fixy::session {

// The gate of mint_session: the gate of a session that starts with the
// empty permission set.
template <typename Ctx, typename Proto, typename Resource>
concept CtxFitsSession = CtxFitsSessionFrom<Ctx, Proto, Resource>;

// Returns the first handle of Proto over the Resource, with the empty
// permission set.  A Proto that starts with a Loop is unrolled one
// iteration, as mint_session_handle unrolls it.
template <typename Proto, AbandonmentPolicy Policy = DefaultAbandonmentPolicy, typename Ctx, typename Resource>
    requires CtxFitsSession<Ctx, Proto, Resource>
[[nodiscard]] constexpr auto mint_session(Ctx const&, Resource resource,
                                          std::source_location loc = std::source_location::current()) noexcept {
    return detail::open_session_<Proto, Resource, Policy, ::foundation::permissions::EmptyPermSet>(
        std::forward<Resource>(resource), loc);
}

// The gate of the context-bound callback: the gate of mint_session, and a
// body that returns the End handle of its own session.
template <typename Ctx, typename Proto, typename Resource, typename Policy, typename Body>
concept CtxFitsSessionBody = CtxFitsSession<Ctx, Proto, Resource> && SessionBody<Body, Proto, Resource, Policy>;

// Runs Proto over the Resource inside the body, and returns the Resource
// that the End handle gives back.  Every handle of the session carries the
// brand of this body, as with_session(resource, body) does.
template <typename Proto, AbandonmentPolicy Policy = DefaultAbandonmentPolicy, typename Ctx, typename Resource,
          typename Body>
    requires CtxFitsSessionBody<Ctx, Proto, Resource, Policy, Body>
[[nodiscard]] constexpr Resource
with_session(Ctx const&, Resource resource, Body body, std::source_location loc = std::source_location::current()) noexcept(
    std::is_nothrow_invocable_v<Body, detail::first_handle_t<Proto, Resource, Policy,
                                                             ::foundation::permissions::EmptyPermSet,
                                                             detail::brand_ctx_t<Body>>>) {
    auto at_end = std::invoke(std::move(body),
                              detail::open_session_<Proto, Resource, Policy, ::foundation::permissions::EmptyPermSet,
                                                    detail::brand_ctx_t<Body>>(std::forward<Resource>(resource), loc));
    return std::move(at_end).close();
}

}  // namespace fixy::session
