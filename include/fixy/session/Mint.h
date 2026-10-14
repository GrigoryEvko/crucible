#pragma once

// The mints of the binary session core.  Each mint states the gate of its
// session, and the door of the mints of fixy/session/Handle.h opens the
// session.
//
// mint_session_handle opens a session with the empty permission set, and
// mint_permissioned_session consumes a token for each tag of its set.
// with_session runs a body on a session that the library owns.
// mint_forked_channel makes a channel and starts its two sides on two
// threads, and mint_test_channel gives a test the two ends of a channel.

#include <fixy/session/Admission.h>
#include <fixy/session/Handle.h>
#include <fixy/session/Protocol.h>
#include <fixy/session/Stepping.h>

#include <foundation/permissions/PermSet.h>
#include <foundation/permissions/Permission.h>

#include <type_traits>
#include <utility>

namespace fixy::session {

// The body static_asserts repeat the concept's checks.  They fire only
// if some route reaches the body without the concept having run, and
// their prose is what explains a rejection the concept states only as a
// boolean.

template <typename Proto, typename Resource, AbandonmentPolicy Policy = DefaultAbandonmentPolicy>
    requires WellFormedRunnableProtocol<Proto> && SessionResource<Resource>
          && PermissionFlowCloses<Proto, ::foundation::permissions::EmptyPermSet>
[[nodiscard]] constexpr auto mint_session_handle(Resource r,
                                                 std::source_location loc = std::source_location::current()) noexcept {
    static_assert(is_well_formed_v<Proto>, "fixy::session::diagnostic [Protocol_Ill_Formed]: "
                                           "protocol is ill-formed.  Most likely cause: a Continue "
                                           "appears outside any enclosing Loop<Body>.  Every Continue must "
                                           "have a Loop above it in the protocol tree.");

    // is_well_formed refuses an empty choice too.  This assertion names
    // the fault, and the assertion above gives the general refusal.
    static_assert(!is_empty_choice_v<Proto>, "fixy::session::diagnostic [Empty_Choice_Combinator]: "
                                             "mint_session_handle<Proto> — Proto contains a "
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
                                             "mint_session_handle<Proto, Resource>: the Resource must be a "
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

    // Forwarding `loc` keeps the caller's site in the abandonment
    // diagnostic even though the unroll inserts an intermediate handle
    // whose own default location would otherwise win.
    return detail::late_door_t<SessionMintDoor, Proto>::template open<Proto, Resource, Policy>(
        std::forward<Resource>(r), loc);
}

// ── A session that starts with permissions ───────────────────────────
//
// The permission set of a handle names the regions whose tokens its
// messages can move.  mint_session_handle starts with the empty set.
// mint_permissioned_session consumes one token for each tag, and starts
// the session with the set of those tags.  It returns the handle and a
// PermHold of the same set.  The handle carries the set as a type, and
// the hold carries the tokens.  The caller takes a token from the hold
// for each message that moves one.  This is the CSL frame rule at the
// start of a session: the set of the handle is backed by tokens that no
// other holder has, and each region has one owner (Actris per-message
// resources).
//
// The gate is one concept, CtxFitsSessionFrom, for every set of tags.  The
// context is an execution context, and it admits the permission row of
// each tag (foundation/permissions/Permission.h).  The tags are distinct.
// The protocol is runnable, the Resource is a SessionResource, and the
// permission flow of the whole protocol closes from the set of the
// tokens, on every branch.  The context holds each effect that a payload
// of the protocol carries, and the row of each permission that the
// protocol delivers to the endpoint (CtxAdmitsProtocolRow).  This mint
// asks for one tag or more.  mint_session of fixy/session/Entry.h reads
// the same concept with no tag.
//
// The tokens are the last parameters, so the mint cannot take a
// source_location after them.  The session records the site of the mint.
// Complexity: linear in the size of the protocol, at compile time.

template <typename Proto, AbandonmentPolicy Policy = DefaultAbandonmentPolicy, typename Ctx, typename Resource,
          typename... Tags, typename... Brands>
    requires CtxFitsPermissionedSession<Ctx, Proto, Resource, Tags...>
[[nodiscard]] constexpr auto
mint_permissioned_session(Ctx const& ctx, Resource resource,
                          ::foundation::permissions::Permission<Tags, Brands>... tokens) noexcept {
    return detail::late_door_t<SessionMintDoor, Proto>::template open_permissioned<Proto, Policy>(
        ctx, std::forward<Resource>(resource), std::move(tokens)...);
}

// ── The callback entry point ─────────────────────────────────────────
//
// The library owns the handle.  It builds the first handle, gives it to
// the body, takes back the handle that the body returns, and closes it.
// The body must return a handle at End, so a body that drops the handle
// or stops early does not compile.  This is the design of Thiemann
// (ICFP 2023): the shape of the API makes the handle linear.
//
// Every handle of the session carries the type of the body as its brand,
// and the entry point accepts back only a handle with that brand.  A body
// cannot mint a new session, walk it to End and return it in its place,
// because no public mint makes a branded handle.

template <typename Proto, typename Resource, AbandonmentPolicy Policy = DefaultAbandonmentPolicy, typename Body>
    requires WellFormedRunnableProtocol<Proto> && SessionResource<Resource>
          && PermissionFlowCloses<Proto, ::foundation::permissions::EmptyPermSet>
          && SessionBody<Body, Proto, Resource, Policy>
[[nodiscard]] constexpr Resource
with_session(Resource r, Body body, std::source_location loc = std::source_location::current()) noexcept(
    std::is_nothrow_invocable_v<Body,
                                detail::first_handle_t<Proto, Resource, Policy, ::foundation::permissions::EmptyPermSet,
                                                       detail::brand_ctx_t<Body>>>) {
    return detail::late_door_t<SessionMintDoor, Proto>::template run<Proto, Resource, Policy>(std::forward<Resource>(r),
                                                                                              std::move(body), loc);
}

// ── Channels ─────────────────────────────────────────────────────────
//
// A channel has two endpoints.  If one thread holds the two endpoints, it
// can wait on one for a message that only the other can send, and it
// waits forever.  LinearActris excludes this by the forest condition:
// the program makes a channel only when it starts a thread, and gives
// one endpoint to each side.  mint_forked_channel is that shape.  No
// public mint in this header gives the two endpoints to one caller.

template <typename Proto, typename ResourceA, typename ResourceB>
void mint_channel(ResourceA, ResourceB) noexcept =
    delete("[Channel_Needs_A_Fork] mint_channel<Proto>(resource_a, resource_b) is removed.  It gives the two "
           "endpoints of one channel to one caller, and a caller that holds the two endpoints can wait on one of "
           "them for a message that only the other can send.  Use mint_forked_channel<Proto, SelfTag, "
           "PeerTag>(ctx, parent, resource_self, resource_peer, self_body, peer_body), which gives each endpoint "
           "to its own thread.  A one-thread test that needs the two endpoints uses mint_test_channel with a "
           "test context.");

// Makes a channel and starts its two sides on two threads through
// mint_permission_fork of fixy/concurrent/PermissionFork.h.  The parent
// permission splits into SelfTag and PeerTag.  The self body runs Proto
// over self_resource, and the peer body runs the dual over peer_resource.
// Each body gets its endpoint, the fork's view of its child permission
// and the context, and must return its endpoint at End.  The call returns
// the parent permission after the two threads join.
//
// No thread holds the two endpoints when the mint makes them.  Each
// endpoint exists in the thread that runs its body, so ownership is a
// tree: the caller owns the two threads, and each thread owns one
// endpoint.  This is the shape the forest condition of LinearActris asks
// for.  A body can still move its handle to another thread through
// shared memory, and then the forest condition does not hold.  The
// brand makes the body get its own handle back before it can return.
template <typename Proto, typename SelfTag, typename PeerTag, AbandonmentPolicy Policy = DefaultAbandonmentPolicy,
          typename Ctx, typename Parent, typename Brand, typename ResourceSelf, typename ResourcePeer,
          typename SelfBody, typename PeerBody>
    requires CtxFitsForkedChannel<Ctx, Proto, Parent, SelfTag, PeerTag> && SessionResource<ResourceSelf>
          && SessionResource<ResourcePeer> && ChannelEndsShareAPriority<ResourceSelf, ResourcePeer>
          && detail::ForkedEndpointBody<SelfBody, Proto, ResourceSelf, Policy, SelfTag, Brand, Ctx>
          && detail::ForkedEndpointBody<PeerBody, dual_of_t<Proto>, ResourcePeer, Policy, PeerTag, Brand, Ctx>
[[nodiscard]] ::foundation::permissions::Permission<Parent, Brand>
mint_forked_channel(Ctx const& ctx, ::foundation::permissions::Permission<Parent, Brand>&& parent,
                    ResourceSelf self_resource, ResourcePeer peer_resource, SelfBody self_body, PeerBody peer_body,
                    std::source_location loc = std::source_location::current()) noexcept {
    return detail::late_door_t<SessionMintDoor, Proto>::template fork_channel<Proto, dual_of_t<Proto>, SelfTag, PeerTag,
                                                                              Policy>(
        ctx, std::move(parent), std::forward<ResourceSelf>(self_resource), std::forward<ResourcePeer>(peer_resource),
        std::move(self_body), std::move(peer_body), loc);
}

// The two ends of one channel to one caller, for a test that drives the
// two sides on one thread.  The context gate, CtxFitsTestChannel of
// fixy/session/Admission.h, admits only a context that holds the Test
// capability.
template <typename Proto, AbandonmentPolicy Policy = DefaultAbandonmentPolicy, typename Ctx, typename ResourceA,
          typename ResourceB>
    requires CtxFitsTestChannel<Ctx, Proto> && SessionResource<ResourceA> && SessionResource<ResourceB>
          && ChannelEndsShareAPriority<ResourceA, ResourceB>
[[nodiscard]] constexpr auto mint_test_channel(Ctx const& ctx, ResourceA resource_a, ResourceB resource_b,
                                               std::source_location loc = std::source_location::current()) noexcept {
    return detail::late_door_t<SessionMintDoor, Proto>::template open_test_channel<Proto, Policy>(
        ctx, std::forward<ResourceA>(resource_a), std::forward<ResourceB>(resource_b), loc);
}

}  // namespace fixy::session
