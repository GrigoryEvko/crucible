#pragma once

// The session that a channel handle runs.  handle_for names the handle of
// each role of a channel, and default_proto_for names the protocol of that
// role.  mint_substrate_session takes the handle by move and gives the
// first session handle, so the session owns the channel handle for its
// whole life.
//
// The table holds the SPSC and the MPSC channel of fixy/concurrent.  Those
// are the only permissioned channels of the new tree, so each other row of
// the old table left with its channel.
//
// Old spelling: include/crucible/concurrent/_SubstrateSessionBridge.h.  The
// old mint took the handle by reference and put its address in the
// session.  fixy/session/Handle.h refuses a raw pointer to a channel handle
// as a session resource, because the handle can move or die first.  So the
// session takes the handle by move.  The old mint also asked that the
// footprint of one call fits the residency tier of the context.  The new
// context carries no residency tier (foundation/effects/Ctx.h), so that
// clause has no carrier.

#include <fixy/concurrent/HandleTraits.h>
#include <fixy/concurrent/PermissionedMpscChannel.h>
#include <fixy/concurrent/PermissionedSpscChannel.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Handle.h>

#include <foundation/effects/Ctx.h>
#include <foundation/reflect/Instance.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace fixy::concurrent {

enum class Direction : std::uint8_t {
    Producer = 0,
    Consumer = 1,
};

namespace detail {

// The two channels of the table.  The set is read by reflection off the
// two templates, so no other type joins it, and a cv-qualified channel is
// not a channel of the table.
template <class Substr>
concept IsBridgedChannel =
    std::same_as<Substr, std::remove_cvref_t<Substr>>
    && ::foundation::reflect::IsInstanceOfAny<Substr, ^^PermissionedSpscChannel, ^^PermissionedMpscChannel>;

}  // namespace detail

// Both channels name their handles ProducerHandle and ConsumerHandle, so
// one rule gives the handle of each role of either channel.
template <class Substr, Direction Dir>
struct handle_for;

template <class Substr, Direction Dir>
    requires detail::IsBridgedChannel<Substr>
struct handle_for<Substr, Dir> {
    using type =
        std::conditional_t<Dir == Direction::Producer, typename Substr::ProducerHandle, typename Substr::ConsumerHandle>;
};

template <class Substr, Direction Dir>
using handle_for_t = typename handle_for<Substr, Dir>::type;

// A loop with no exit branch is the deliberate spelling of a stream that
// runs until it is torn down.  Shutdown is a detach carrying a typed
// reason, detach_reason::InfiniteLoopProtocol, not a branch the protocol
// offers.  The detach releases the channel handle that the session owns.

template <class Substr, Direction Dir>
struct default_proto_for;

template <class Substr, Direction Dir>
    requires detail::IsBridgedChannel<Substr>
struct default_proto_for<Substr, Dir> {
    using type = std::conditional_t<
        Dir == Direction::Producer,
        ::fixy::session::Loop<::fixy::session::Send<typename Substr::value_type, ::fixy::session::Continue>>,
        ::fixy::session::Loop<::fixy::session::Recv<typename Substr::value_type, ::fixy::session::Continue>>>;
};

template <class Substr, Direction Dir>
using default_proto_for_t = typename default_proto_for<Substr, Dir>::type;

namespace detail {

template <class Substr, Direction Dir>
concept HasHandleFor = requires { typename handle_for<Substr, Dir>::type; };

template <class Substr, Direction Dir>
concept HasDefaultProtoFor = requires { typename default_proto_for<Substr, Dir>::type; };

// The handle of a direction has the pole shape of that direction, so a
// stage can take it.  fixy/concurrent/StageShape.h reads the same shape.
template <class Substr, Direction Dir>
concept HandleHasPoleOf = (Dir == Direction::Producer && is_producer_handle_v<handle_for_t<Substr, Dir>>)
                       || (Dir == Direction::Consumer && is_consumer_handle_v<handle_for_t<Substr, Dir>>);

}  // namespace detail

// A direction is bridgeable when the table names both its handle and its
// protocol, and the handle has the pole shape of the direction.  Only the
// two channels have a row, so the concept is closed over them.
template <class Substr, Direction Dir>
concept IsBridgeableDirection =
    detail::HasHandleFor<Substr, Dir> && detail::HasDefaultProtoFor<Substr, Dir> && detail::HandleHasPoleOf<Substr, Dir>;

// The gate of mint_substrate_session: a bridgeable direction, and the gate
// of fixy::session::mint_session for its protocol over its handle.  So the
// context admits each effect that a payload of the protocol carries.
template <class Substr, Direction Dir, class Ctx>
concept CtxFitsSubstrateSessionMint =
    ::foundation::effects::IsExecCtx<Ctx> && IsBridgeableDirection<Substr, Dir>
    && ::fixy::session::CtxFitsSession<Ctx, default_proto_for_t<Substr, Dir>, handle_for_t<Substr, Dir>>;

// The handle comes in by move.  The caller keeps only a moved-from handle,
// which reaches no channel, so the session and the caller never act on the
// channel at the same time.
template <class Substr, Direction Dir, ::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsSubstrateSessionMint<Substr, Dir, Ctx>
[[nodiscard]] constexpr auto mint_substrate_session(Ctx const& ctx, handle_for_t<Substr, Dir>&& handle) noexcept {
    return ::fixy::session::mint_session<default_proto_for_t<Substr, Dir>>(ctx, std::move(handle));
}

namespace detail::substrate_session_bridge_self_test {

namespace proto = ::fixy::session;

struct UserTag {};

using Spsc = PermissionedSpscChannel<int, 64, UserTag>;
using Mpsc = PermissionedMpscChannel<int, 64, UserTag>;

static_assert(std::is_same_v<handle_for_t<Spsc, Direction::Producer>, typename Spsc::ProducerHandle>);
static_assert(std::is_same_v<handle_for_t<Spsc, Direction::Consumer>, typename Spsc::ConsumerHandle>);
static_assert(std::is_same_v<handle_for_t<Mpsc, Direction::Producer>, typename Mpsc::ProducerHandle>);
static_assert(std::is_same_v<handle_for_t<Mpsc, Direction::Consumer>, typename Mpsc::ConsumerHandle>);

static_assert(
    std::is_same_v<default_proto_for_t<Spsc, Direction::Producer>, proto::Loop<proto::Send<int, proto::Continue>>>);
static_assert(
    std::is_same_v<default_proto_for_t<Spsc, Direction::Consumer>, proto::Loop<proto::Recv<int, proto::Continue>>>);
static_assert(
    std::is_same_v<default_proto_for_t<Mpsc, Direction::Producer>, proto::Loop<proto::Send<int, proto::Continue>>>);
static_assert(
    std::is_same_v<default_proto_for_t<Mpsc, Direction::Consumer>, proto::Loop<proto::Recv<int, proto::Continue>>>);

static_assert(IsBridgeableDirection<Spsc, Direction::Producer>);
static_assert(IsBridgeableDirection<Spsc, Direction::Consumer>);
static_assert(IsBridgeableDirection<Mpsc, Direction::Producer>);
static_assert(IsBridgeableDirection<Mpsc, Direction::Consumer>);

static_assert(!IsBridgeableDirection<int, Direction::Producer>);
static_assert(!IsBridgeableDirection<int, Direction::Consumer>);
static_assert(!IsBridgeableDirection<Spsc const, Direction::Producer>, "a cv-qualified channel has no row");

// A type that names the two handles and a value type is still no channel
// of the table.
struct LooksLikeAChannel {
    using value_type = int;
    using ProducerHandle = typename Spsc::ProducerHandle;
    using ConsumerHandle = typename Spsc::ConsumerHandle;
};
static_assert(!IsBridgeableDirection<LooksLikeAChannel, Direction::Producer>);

}  // namespace detail::substrate_session_bridge_self_test

}  // namespace fixy::concurrent
