#pragma once

// The mints of the binary session core.  Each mint states the gate of its
// session, and the door of the mints of fixy/session/Handle.h opens the
// session.
//
// mint_permissioned_session consumes a token for each tag of its set.
// mint_forked_channel makes a channel and starts its two sides on two
// threads, and mint_test_channel gives a test the two ends of a channel.
// mint_session and with_session of fixy/session/Entry.h open a session
// with the empty permission set.
//
// Each mint takes the execution context of the caller.  Its gate admits
// the effect row of each payload of the protocol in that context, and no
// mint opens a session with no context.

#include <fixy/session/Admission.h>
#include <fixy/session/Handle.h>
#include <fixy/session/Protocol.h>
#include <fixy/session/Stepping.h>

#include <foundation/permissions/PermSet.h>
#include <foundation/permissions/Permission.h>

#include <type_traits>
#include <utility>

namespace fixy::session {

// ── A session that starts with permissions ───────────────────────────
//
// The permission set of a handle names the regions whose tokens its
// messages can move.  mint_session starts with the empty set.
// mint_permissioned_session consumes one token for each tag, and starts
// the session with the set of those tokens.  Each element of the set is the
// type of its token, with its brand (fixy/session/Classification.h).  It
// returns the first handle, which carries the set as a type and keeps the
// tokens in its slots.  Each step that moves a token takes it out of the
// slots, so the caller gives the value of a marker and never its token.  This
// is the CSL frame rule at the start of a session: the set of the handle is
// backed by tokens that no other holder has, and each region has one owner
// (Actris per-message resources).
//
// The gate is one concept, CtxFitsSessionFrom, for every set of tokens.  The
// context is an execution context, and it admits the permission row of
// each tag (foundation/permissions/Permission.h).  The tags are distinct.
// The protocol is runnable, the Resource is a SessionResource, and the
// permission flow of the whole protocol closes from the set of the
// tokens, on every branch.  So each marker of the protocol names the brand
// of the token that it moves.  The context holds each effect that a payload
// of the protocol carries, and the row of each permission that the
// protocol delivers to the endpoint (CtxAdmitsProtocolRow).  This mint
// asks for one token or more.  mint_session of fixy/session/Entry.h reads
// the same concept with no token.
//
// The tokens are the last parameters, so the mint cannot take a
// source_location after them.  The session records the site of the mint.
// Complexity: linear in the size of the protocol, at compile time.

template <typename Proto, AbandonmentPolicy Policy = DefaultAbandonmentPolicy, typename Ctx, typename Resource,
          typename... Tags, typename... Brands>
    requires CtxFitsPermissionedSession<Ctx, Proto, Resource, ::foundation::permissions::Permission<Tags, Brands>...>
[[nodiscard]] constexpr auto
mint_permissioned_session(Ctx const& ctx, Resource resource,
                          ::foundation::permissions::Permission<Tags, Brands>... tokens) noexcept {
    return detail::late_door_t<SessionMintDoor, Proto>::template open_permissioned<Proto, Policy>(
        ctx, std::forward<Resource>(resource), std::move(tokens)...);
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
    requires CtxFitsForkedChannel<Ctx, Proto, Parent, SelfTag, PeerTag> && PlainSessionResource<ResourceSelf>
          && PlainSessionResource<ResourcePeer> && ChannelEndsShareAPriority<ResourceSelf, ResourcePeer>
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
    requires CtxFitsTestChannel<Ctx, Proto> && PlainSessionResource<ResourceA> && PlainSessionResource<ResourceB>
          && ChannelEndsShareAPriority<ResourceA, ResourceB>
[[nodiscard]] constexpr auto mint_test_channel(Ctx const& ctx, ResourceA resource_a, ResourceB resource_b,
                                               std::source_location loc = std::source_location::current()) noexcept {
    return detail::late_door_t<SessionMintDoor, Proto>::template open_test_channel<Proto, Policy>(
        ctx, std::forward<ResourceA>(resource_a), std::forward<ResourceB>(resource_b), loc);
}

}  // namespace fixy::session
