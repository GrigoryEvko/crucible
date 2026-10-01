// The compile-time checks of fixy/concurrent/SubstrateSessionBridge.h.

#include <fixy/concurrent/SubstrateSessionBridge.h>

namespace fixy::concurrent {

namespace detail::substrate_session_bridge_self_test {

namespace proto = ::fixy::session;

struct UserTag {};
struct UserBrand {};

using Spsc = PermissionedSpscChannel<int, 64, UserTag, UserBrand>;
using Mpsc = PermissionedMpscChannel<int, 64, UserTag, UserBrand>;

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
