#pragma once

// The context-bound entry points of a session that starts with no
// permission.  mint_session gives the first handle to the caller.
// with_session gives the first handle to a body, and gives the Resource
// back when the body brings the session to End.
//
// ── The gate ────────────────────────────────────────────────────────
//
// CtxFitsSession is CtxFitsSessionFrom of fixy/session/Admission.h with no
// tag, so it is the gate of mint_permissioned_session at the empty set.
// The two mints cannot drift apart, because they read one concept.  With
// no tag, the context part of the gate asks only that the context is an
// execution context: no tag brings a permission row for the context to
// admit.
//
// The gate also asks that the context holds each effect that a payload of
// the protocol carries (CtxAdmitsProtocolRow of fixy/session/Admission.h).
// A context with no Alloc capability cannot start a session whose
// messages carry an Alloc computation, in either direction, or in a
// protocol that the session delegates.  The context must also admit the
// row of each permission that a receive of the protocol delivers.  A
// context with no IO capability cannot receive a token of a region whose
// touch does IO.
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
#include <fixy/session/Mint.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/PermSet.h>

#include <type_traits>
#include <utility>

namespace fixy::session {

// The gate of mint_session: the gate of a session that starts with the
// empty permission set.
template <typename Ctx, typename Proto, typename Resource>
concept CtxFitsSession = CtxFitsSessionFrom<Ctx, Proto, Resource>;

// Returns the first handle of Proto over the Resource, with the empty
// permission set.  A Proto that starts with a Loop is unrolled one
// iteration.
//
// The body static_asserts repeat the checks of the gate.  They fire only
// if a route reaches the body without the gate, and their text explains a
// refusal that the gate states only as a boolean.
template <typename Proto, AbandonmentPolicy Policy = DefaultAbandonmentPolicy, typename Ctx, typename Resource>
    requires CtxFitsSession<Ctx, Proto, Resource>
[[nodiscard]] constexpr auto mint_session(Ctx const& ctx, Resource resource, MintSite site = {}) noexcept {
    static_assert(is_well_formed_v<Proto>, "fixy::session::diagnostic [Protocol_Ill_Formed]: "
                                           "protocol is ill-formed.  Most likely cause: a Continue "
                                           "appears outside any enclosing Loop<Body>.  Every Continue must "
                                           "have a Loop above it in the protocol tree.");

    // is_well_formed refuses an empty choice too.  This assertion names
    // the fault, and the assertion above gives the general refusal.
    static_assert(!is_empty_choice_v<Proto>, "fixy::session::diagnostic [Empty_Choice_Combinator]: "
                                             "mint_session<Proto> — Proto contains a "
                                             "reachable empty Select<> / Offer<> / Offer<Sender<R>> "
                                             "(top-level or nested under Send/Recv/Loop/branch).  "
                                             "Cannot construct a runnable handle: Select<> has "
                                             "no branch for select<I>() to choose; Offer<> has no label "
                                             "the peer can signal.  The trait walks recursively so "
                                             "nested empties are caught at mint time, not at the "
                                             "eventual select<I>() / recv() that hits the dead-end.  "
                                             "Subtyping refuses an empty choice as well, because a "
                                             "substitute of that type never sends.  Add one branch or "
                                             "more at every reachable choice position, for example "
                                             "Select<Send<Stop, End>>.");

    static_assert(SessionResource<Resource>, "fixy::session::diagnostic [SessionResource_Refused]: "
                                             "mint_session<Proto>(ctx, resource): the Resource must be a "
                                             "value the handle owns, or an lvalue reference to a type "
                                             "derived from foundation::Pinned<T>.  A value that can be "
                                             "copied must be fixy::SelfContained: a copy of a Resource that "
                                             "reaches a channel is a second channel to the peer.  Either: "
                                             "(a) give the Resource a [[no_unique_address]] "
                                             "fixy::session::MoveOnlyResource member, or (b) make the "
                                             "channel Pinned and pass it by lvalue reference.  A raw "
                                             "pointer, a function pointer and a std::reference_wrapper are "
                                             "refused.  An rvalue-reference Resource binds to a temporary "
                                             "and is refused too.");

    static_assert(!std::is_same_v<Proto, Continue>, "fixy::session::diagnostic [Continue_Without_Loop]: "
                                                    "Continue cannot be the top-level protocol.");

    // Forwarding `site` keeps the caller's site in the abandonment
    // diagnostic and in the record of the watch, although the unroll inserts
    // an intermediate handle whose own default location would otherwise win.
    return detail::late_door_t<SessionMintDoor, Proto>::template open<Proto, Policy, Ctx, Resource>(
        ctx, std::forward<Resource>(resource), site);
}

// The gate of the context-bound callback: the gate of mint_session, and a
// body that returns the End handle of its own session.
template <typename Ctx, typename Proto, typename Resource, typename Policy, typename Body>
concept CtxFitsSessionBody = CtxFitsSession<Ctx, Proto, Resource> && SessionBody<Body, Proto, Resource, Policy>;

// Runs Proto over the Resource inside the body, and returns the Resource
// that the End handle gives back.  The library owns the handle.  It builds
// the first handle, gives it to the body, takes back the handle that the
// body returns, and closes it.
//
// Every handle of the session carries the type of the body as its brand,
// and the entry point accepts back only a handle with that brand.  A body
// cannot mint a new session, walk it to End and return it in its place,
// because no public mint makes a branded handle.
//
// The calling thread runs the body and closes the session, so each handle
// of the session lives on that thread.  The session opens under the owned
// form of the policy (check::detail::owned_policy_t of
// fixy/session/Stepping.h): check::Enforced gives check::Pinned, and
// check::Cancel gives check::PinnedCancel.  A step then makes no holder
// check, and no door of foundation/Transfer.h moves a handle of the body to
// another thread.
template <typename Proto, AbandonmentPolicy Policy = DefaultAbandonmentPolicy, typename Ctx, typename Resource,
          typename Body>
    requires CtxFitsSessionBody<Ctx, Proto, Resource, Policy, Body>
[[nodiscard]] constexpr Resource
with_session(Ctx const& ctx, Resource resource, Body body, MintSite site = {}) noexcept(
    std::is_nothrow_invocable_v<Body, detail::owned_head_t<Proto, Resource, Policy, Body>>) {
    return detail::late_door_t<SessionMintDoor, Proto>::template run<Proto, Policy, Ctx, Resource>(
        ctx, std::forward<Resource>(resource), std::move(body), site);
}

}  // namespace fixy::session
