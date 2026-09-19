#pragma once

#include <crucible/Platform.h>
#include <crucible/concurrent/PermissionedMpmcChannel.h>
#include <crucible/permissions/_Permission.h>
#include <crucible/sessions/PermissionedSession.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionMint.h>

#include <concepts>
#include <cstddef>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>

namespace crucible::safety::proto::mpmc_channel_session {

// Neither protocol has an exit branch, so neither ever reaches End. The caller
// detaches the terminal handle at shutdown.
//
// A protocol belongs to one endpoint handle, not to the channel. Many endpoints
// run their own session over the same channel at the same time.

template <typename T>
using ProducerProto = Loop<Send<T, Continue>>;

template <typename T>
using ConsumerProto = Loop<Recv<T, Continue>>;

template <typename Channel>
concept MpmcChannelSessionSurface =
    requires(Channel& ch, typename Channel::ProducerHandle& producer_handle,
             typename Channel::ConsumerHandle& consumer_handle, const typename Channel::value_type& sample_payload) {
        typename Channel::value_type;
        typename Channel::user_tag;
        typename Channel::producer_tag;
        typename Channel::consumer_tag;
        typename Channel::ProducerHandle;
        typename Channel::ConsumerHandle;

        { ch.producer() } -> std::same_as<std::optional<typename Channel::ProducerHandle>>;
        { ch.consumer() } -> std::same_as<std::optional<typename Channel::ConsumerHandle>>;

        { producer_handle.try_push(sample_payload) } -> std::same_as<bool>;

        { consumer_handle.try_pop() } -> std::same_as<std::optional<typename Channel::value_type>>;
    };

template <MpmcChannelSessionSurface Channel>
[[nodiscard]] auto mint_mpmc_producer_endpoint(Channel& ch) noexcept
    -> std::optional<typename Channel::ProducerHandle> {
    return ch.producer();
}

template <MpmcChannelSessionSurface Channel>
[[nodiscard]] auto mint_mpmc_consumer_endpoint(Channel& ch) noexcept
    -> std::optional<typename Channel::ConsumerHandle> {
    return ch.consumer();
}

// The session stores the address of the handle rather than the handle. The
// handle outlives the session it is bound to.

template <MpmcChannelSessionSurface Channel, ::crucible::effects::IsExecCtx Ctx>
[[nodiscard]] constexpr auto mint_mpmc_producer_session(Ctx const& ctx,
                                                        typename Channel::ProducerHandle& handle) noexcept {
    using T = typename Channel::value_type;
    return mint_permissioned_session<ProducerProto<T>>(ctx, &handle);
}

template <MpmcChannelSessionSurface Channel, ::crucible::effects::IsExecCtx Ctx>
[[nodiscard]] constexpr auto mint_mpmc_consumer_session(Ctx const& ctx,
                                                        typename Channel::ConsumerHandle& handle) noexcept {
    using T = typename Channel::value_type;
    return mint_permissioned_session<ConsumerProto<T>>(ctx, &handle);
}

template <MpmcChannelSessionSurface Channel, ::crucible::effects::IsExecCtx Ctx = ::crucible::effects::HotFgCtx>
using ProducerSessionHandle = decltype(mint_mpmc_producer_session<Channel>(
    std::declval<Ctx const&>(), std::declval<typename Channel::ProducerHandle&>()));

template <MpmcChannelSessionSurface Channel, ::crucible::effects::IsExecCtx Ctx = ::crucible::effects::HotFgCtx>
using ConsumerSessionHandle = decltype(mint_mpmc_consumer_session<Channel>(
    std::declval<Ctx const&>(), std::declval<typename Channel::ConsumerHandle&>()));

inline constexpr auto blocking_push = [](auto& hp, auto&& value) noexcept {
    while (!hp->try_push(std::forward<decltype(value)>(value))) {
        CRUCIBLE_SPIN_PAUSE;
    }
};

inline constexpr auto blocking_pop = [](auto& hp) noexcept {
    for (;;) {
        if (auto v = hp->try_pop()) return *v;
        CRUCIBLE_SPIN_PAUSE;
    }
};

}  // namespace crucible::safety::proto::mpmc_channel_session

namespace crucible::safety::proto::mpmc_channel_session::detail::sizeof_witness {

struct Tag {};
using SmallChannel = ::crucible::concurrent::PermissionedMpmcChannel<int, 16, Tag>;
using ProdHandle = SmallChannel::ProducerHandle;
using ConsHandle = SmallChannel::ConsumerHandle;

static_assert(MpmcChannelSessionSurface<SmallChannel>,
              "mpmc_channel_session: PermissionedMpmcChannel must satisfy "
              "MpmcChannelSessionSurface — if this fails, the channel's "
              "endpoint shape (producer()/consumer() returning optional<Handle>) "
              "has drifted and the session facade can no longer wrap it.");

static_assert(std::is_same_v<ProducerProto<int>, Loop<Send<int, Continue>>>);
static_assert(std::is_same_v<ConsumerProto<int>, Loop<Recv<int, Continue>>>);

// The size equality is asserted on the concrete head types a mint returns after
// the Loop unrolls, not on Loop itself. Loop is a shape-only template with no
// handle specialisation, so it has no size to compare.

static_assert(sizeof(PermissionedSessionHandle<End, EmptyPermSet, ProdHandle*>)
                  == sizeof(SessionHandle<End, ProdHandle*>),
              "mpmc_channel_session: PSH<End, EmptyPermSet, ProdHandle*> "
              "must be same size as bare SessionHandle<End, ProdHandle*> — "
              "if this fails, EBO collapse of EmptyPermSet has been broken "
              "or the abandonment tracker grew asymmetrically between PSH "
              "and bare.");

static_assert(sizeof(PermissionedSessionHandle<End, EmptyPermSet, ConsHandle*>)
                  == sizeof(SessionHandle<End, ConsHandle*>),
              "mpmc_channel_session: PSH<End, EmptyPermSet, ConsHandle*> "
              "must be same size as bare SessionHandle<End, ConsHandle*>.");

static_assert(sizeof(PermissionedSessionHandle<Send<int, End>, EmptyPermSet, ProdHandle*>)
                  == sizeof(SessionHandle<Send<int, End>, ProdHandle*>),
              "mpmc_channel_session: PSH<Send<int, End>, EmptyPermSet, "
              "ProdHandle*> must be same size as bare SessionHandle for "
              "the same head.");

static_assert(sizeof(PermissionedSessionHandle<Recv<int, End>, EmptyPermSet, ConsHandle*>)
                  == sizeof(SessionHandle<Recv<int, End>, ConsHandle*>),
              "mpmc_channel_session: PSH<Recv<int, End>, EmptyPermSet, "
              "ConsHandle*> must be same size as bare SessionHandle for "
              "the same head.");

}  // namespace crucible::safety::proto::mpmc_channel_session::detail::sizeof_witness
