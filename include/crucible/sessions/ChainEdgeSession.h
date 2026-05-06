#pragma once

// ChainEdgeSession.h — typed-session facade for PermissionedChainEdge.
//
// ChainEdge is one-shot causal sequencing between two execution plans:
// the upstream role emits a SemaphoreSignal, and the downstream role
// waits for the same value.  The session surface mirrors that shape:
//
//   SignalerProto = Send<SemaphoreSignal, End>
//   WaiterProto   = Recv<SemaphoreSignal, End>

#include <crucible/Platform.h>
#include <crucible/concurrent/PermissionedChainEdge.h>
#include <crucible/permissions/Permission.h>
#include <crucible/sessions/PermissionedSession.h>
#include <crucible/sessions/Session.h>

#include <concepts>
#include <thread>
#include <type_traits>
#include <utility>

namespace crucible::safety::proto::chainedge_session {

using Signal = ::crucible::concurrent::SemaphoreSignal;

using SignalerProto = Send<Signal, End>;
using WaiterProto   = Recv<Signal, End>;

template <typename Edge>
concept ChainEdgeSessionSurface = requires(
    Edge& edge,
    typename Edge::SignalerHandle& signaler,
    typename Edge::WaiterHandle& waiter,
    ::crucible::safety::Permission<typename Edge::signaler_tag> sig_perm,
    ::crucible::safety::Permission<typename Edge::waiter_tag> wait_perm,
    const Signal& signal)
{
    typename Edge::value_type;
    typename Edge::signaler_tag;
    typename Edge::waiter_tag;
    typename Edge::SignalerHandle;
    typename Edge::WaiterHandle;

    requires std::same_as<typename Edge::value_type, Signal>;
    requires std::same_as<typename Edge::SignalerHandle::value_type, Signal>;
    requires std::same_as<typename Edge::WaiterHandle::value_type, Signal>;

    { edge.signaler(std::move(sig_perm)) }
        -> std::same_as<typename Edge::SignalerHandle>;
    { edge.waiter(std::move(wait_perm)) }
        -> std::same_as<typename Edge::WaiterHandle>;
    { signaler.expected_signal() } -> std::same_as<Signal>;
    { signaler.signal(signal) } -> std::same_as<void>;
    { waiter.expected_signal() } -> std::same_as<Signal>;
    { waiter.try_wait(signal) } -> std::same_as<bool>;
};

template <ChainEdgeSessionSurface Edge>
[[nodiscard]] auto mint_chainedge_signaler(
    Edge& edge,
    ::crucible::safety::Permission<typename Edge::signaler_tag>&& perm) noexcept
{
    return edge.signaler(std::move(perm));
}

template <ChainEdgeSessionSurface Edge>
[[nodiscard]] auto mint_chainedge_waiter(
    Edge& edge,
    ::crucible::safety::Permission<typename Edge::waiter_tag>&& perm) noexcept
{
    return edge.waiter(std::move(perm));
}

template <ChainEdgeSessionSurface Edge>
[[nodiscard]] constexpr auto
mint_chainedge_signaler_session(typename Edge::SignalerHandle& handle) noexcept
{
    return mint_permissioned_session<SignalerProto,
                                     typename Edge::SignalerHandle*>(&handle);
}

template <ChainEdgeSessionSurface Edge>
[[nodiscard]] constexpr auto
mint_chainedge_waiter_session(typename Edge::WaiterHandle& handle) noexcept
{
    return mint_permissioned_session<WaiterProto,
                                     typename Edge::WaiterHandle*>(&handle);
}

template <ChainEdgeSessionSurface Edge>
using SignalerSessionHandle = decltype(
    mint_chainedge_signaler_session<Edge>(
        std::declval<typename Edge::SignalerHandle&>()));

template <ChainEdgeSessionSurface Edge>
using WaiterSessionHandle = decltype(
    mint_chainedge_waiter_session<Edge>(
        std::declval<typename Edge::WaiterHandle&>()));

inline constexpr auto signal_transport = [](auto& hp, const Signal& signal) noexcept {
    hp->signal(signal);
};

inline constexpr auto wait_transport = [](auto& hp) noexcept {
    for (;;) {
        const Signal signal = hp->expected_signal();
        if (hp->try_wait(signal)) {
            return signal;
        }
        std::this_thread::yield();
    }
};

namespace detail::chainedge_session_self_test {

struct Tag {};
using Edge = ::crucible::concurrent::PermissionedChainEdge<
    ::crucible::concurrent::VendorBackend::CPU, Tag>;
using SignalerHandle = Edge::SignalerHandle;
using WaiterHandle = Edge::WaiterHandle;
using SignalerSession = SignalerSessionHandle<Edge>;
using WaiterSession = WaiterSessionHandle<Edge>;

static_assert(ChainEdgeSessionSurface<Edge>);
static_assert(std::is_same_v<SignalerProto, Send<Signal, End>>);
static_assert(std::is_same_v<WaiterProto, Recv<Signal, End>>);
static_assert(std::is_same_v<typename SignalerSession::protocol,
                             SignalerProto>);
static_assert(std::is_same_v<typename WaiterSession::protocol,
                             WaiterProto>);
static_assert(std::is_same_v<typename SignalerSession::perm_set,
                             EmptyPermSet>);
static_assert(std::is_same_v<typename WaiterSession::perm_set,
                             EmptyPermSet>);

static_assert(sizeof(SignalerHandle) == sizeof(Edge*));
static_assert(sizeof(WaiterHandle) == sizeof(Edge*));
static_assert(sizeof(PermissionedSessionHandle<End, EmptyPermSet,
                                                SignalerHandle*>)
              == sizeof(SessionHandle<End, SignalerHandle*>));
static_assert(sizeof(PermissionedSessionHandle<End, EmptyPermSet,
                                                WaiterHandle*>)
              == sizeof(SessionHandle<End, WaiterHandle*>));

}  // namespace detail::chainedge_session_self_test

}  // namespace crucible::safety::proto::chainedge_session
