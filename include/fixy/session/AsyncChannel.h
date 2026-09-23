#pragma once

// ── A forked channel whose sides are related by asynchronous subtyping ─
//
// mint_forked_channel makes a channel whose two sides are exact duals.
// A side can also run a protocol that sends ahead of its peer, when the
// channel buffers the messages that it sends ahead.  The asynchronous
// relation of fixy/session/Subtype.h admits such a pair up to a capacity.
// The capacity is the number of messages the channel holds in one
// direction.
//
// A check at a capacity that the channel does not have is not sound.  A
// pair that the check admits at capacity 4 can deadlock on a channel of
// capacity 1, because each side then waits on a full buffer.  So this
// mint does not take the capacity from the caller.  It reads the capacity
// that the two Resources state, and it runs the check at that capacity.
//
// A Resource states its capacity with a static constant member:
//
//   struct RingEnd {
//       static constexpr std::size_t channel_capacity = 1;
//       ...
//   };
//
// The two Resources of one channel must state the same capacity.  A
// Resource that states none is refused, because the mint then has no
// capacity it can trust.
//
// The rest of the mint is the fork shape of mint_forked_channel: each
// side runs on its own thread with its own endpoint, and each body
// returns its endpoint at End.

#include <fixy/session/Handle.h>
#include <fixy/session/Subtype.h>

#include <foundation/permissions/PermSet.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/PermissionFork.h>

#include <concepts>
#include <cstddef>
#include <source_location>
#include <type_traits>
#include <utility>

namespace fixy::session {

// True when the Resource states a channel capacity of one message or more.
template <typename Resource>
concept StatesChannelCapacity = requires {
    { std::remove_cvref_t<Resource>::channel_capacity } -> std::convertible_to<std::size_t>;
} && (static_cast<std::size_t>(std::remove_cvref_t<Resource>::channel_capacity) > 0);

template <typename Resource>
    requires StatesChannelCapacity<Resource>
inline constexpr std::size_t channel_capacity_v =
    static_cast<std::size_t>(std::remove_cvref_t<Resource>::channel_capacity);

// The gate of the asynchronous fork-shaped mint.  Each side is runnable,
// its permission flow closes, the two Resources state one capacity, the
// self side refines the dual of the peer side at that capacity, and the
// context may start the fork.
template <typename Ctx, typename SelfProto, typename PeerProto, typename Parent, typename SelfTag, typename PeerTag,
          typename ResourceSelf, typename ResourcePeer>
concept CtxFitsAsyncForkedChannel =
    WellFormedRunnableProtocol<SelfProto> && WellFormedRunnableProtocol<PeerProto>
    && PermissionFlowCloses<SelfProto, ::foundation::permissions::EmptyPermSet>
    && PermissionFlowCloses<PeerProto, ::foundation::permissions::EmptyPermSet>
    && StatesChannelCapacity<ResourceSelf> && StatesChannelCapacity<ResourcePeer>
    && (channel_capacity_v<ResourceSelf> == channel_capacity_v<ResourcePeer>)
    && is_subtype_async_v<SelfProto, dual_of_t<PeerProto>, channel_capacity_v<ResourceSelf>>
    && ::foundation::permissions::CtxFitsPermissionFork<Ctx, Parent, SelfTag, PeerTag>;

// Makes a channel whose self side runs SelfProto and whose peer side runs
// PeerProto, and starts the two sides on two threads.  The call returns
// the parent permission after the two threads join.  The deadlock-freedom
// scope of fixy/session/Handle.h applies: ownership must stay a forest.
template <typename SelfProto, typename PeerProto, typename SelfTag, typename PeerTag,
          AbandonmentPolicy Policy = DefaultAbandonmentPolicy, typename Ctx, typename Parent, typename Brand,
          typename ResourceSelf, typename ResourcePeer, typename SelfBody, typename PeerBody>
    requires CtxFitsAsyncForkedChannel<Ctx, SelfProto, PeerProto, Parent, SelfTag, PeerTag, ResourceSelf, ResourcePeer>
          && SessionResource<ResourceSelf> && SessionResource<ResourcePeer>
          && detail::ForkedEndpointBody<SelfBody, SelfProto, ResourceSelf, Policy, SelfTag, Ctx>
          && detail::ForkedEndpointBody<PeerBody, PeerProto, ResourcePeer, Policy, PeerTag, Ctx>
// §XXI carve-out: cx=alloc — starting a thread is a kernel side effect.
[[nodiscard]] ::foundation::permissions::Permission<Parent, Brand>
mint_forked_async_channel(Ctx const& ctx, ::foundation::permissions::Permission<Parent, Brand>&& parent,
                          ResourceSelf self_resource, ResourcePeer peer_resource, SelfBody self_body,
                          PeerBody peer_body, std::source_location loc = std::source_location::current()) noexcept {
    using SelfSide = detail::forked_endpoint_<SelfProto, Policy, ResourceSelf, SelfBody>;
    using PeerSide = detail::forked_endpoint_<PeerProto, Policy, ResourcePeer, PeerBody>;
    return ::foundation::permissions::mint_permission_fork<SelfTag, PeerTag>(
        ctx, std::move(parent), SelfSide{std::forward<ResourceSelf>(self_resource), std::move(self_body), loc},
        PeerSide{std::forward<ResourcePeer>(peer_resource), std::move(peer_body), loc});
}

}  // namespace fixy::session
