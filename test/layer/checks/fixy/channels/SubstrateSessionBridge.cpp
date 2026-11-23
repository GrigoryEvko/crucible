// The compile-time checks of fixy/channels/SubstrateSessionBridge.h.

#include <fixy/channels/SubstrateSessionBridge.h>

namespace fixy::channels {

namespace detail::substrate_session_bridge_self_test {

namespace proto = ::fixy::session;

struct UserTag {};
struct UserBrand {};

using Spsc =
    ::fixy::concurrent::PermissionedChannel<::fixy::concurrent::ChannelForm::wire, int, 64, UserTag, UserBrand>;
using Mpsc =
    ::fixy::concurrent::PermissionedChannel<::fixy::concurrent::ChannelForm::merge, int, 64, UserTag, UserBrand>;

static_assert(std::is_same_v<end_for_t<Spsc, Direction::Producer>, typename Spsc::ProducerEnd>);
static_assert(std::is_same_v<end_for_t<Spsc, Direction::Consumer>, typename Spsc::ConsumerEnd>);
static_assert(std::is_same_v<end_for_t<Mpsc, Direction::Producer>, typename Mpsc::ProducerEnd>);
static_assert(std::is_same_v<end_for_t<Mpsc, Direction::Consumer>, typename Mpsc::ConsumerEnd>);

// A scope brand that the checks name, and the frame handles of the ends on
// it.  No frame door gives a handle on it.
struct CheckScope {};
using SpscProducer = ::fixy::concurrent::ProducerHandle<Spsc, CheckScope>;
using MpscConsumer = ::fixy::concurrent::ConsumerHandle<Mpsc, CheckScope>;
static_assert(FrameHandleOfDirection<SpscProducer, Spsc, Direction::Producer>);
static_assert(!FrameHandleOfDirection<SpscProducer, Spsc, Direction::Consumer>, "a producer has no other direction");
static_assert(!FrameHandleOfDirection<typename Spsc::ProducerEnd, Spsc, Direction::Producer>,
              "a session runs over a frame handle, and no code outside the frame doors holds an end");
static_assert(!FrameHandleOfDirection<SpscProducer&, Spsc, Direction::Producer>,
              "a reference to a handle is no handle");
static_assert(FrameHandleOfDirection<MpscConsumer, Mpsc, Direction::Consumer>);
static_assert(PoleOfDirection<typename Spsc::ProducerEnd, Spsc, Direction::Producer>
              && PoleOfDirection<SpscProducer, Spsc, Direction::Producer>);

static_assert(
    std::is_same_v<default_proto_for_t<Spsc, Direction::Producer>, proto::Loop<proto::Send<int, proto::Continue>>>);
static_assert(
    std::is_same_v<default_proto_for_t<Spsc, Direction::Consumer>, proto::Loop<proto::Recv<int, proto::Continue>>>);
static_assert(
    std::is_same_v<default_proto_for_t<Mpsc, Direction::Producer>, proto::Loop<proto::Send<int, proto::Continue>>>);
static_assert(
    std::is_same_v<default_proto_for_t<Mpsc, Direction::Consumer>, proto::Loop<proto::Recv<int, proto::Continue>>>);

// The consumer end of each channel runs the dual of its producer end.
static_assert(std::is_same_v<default_proto_for_t<Spsc, Direction::Consumer>,
                             proto::dual_of_t<default_proto_for_t<Spsc, Direction::Producer>>>);
static_assert(std::is_same_v<default_proto_for_t<Mpsc, Direction::Consumer>,
                             proto::dual_of_t<default_proto_for_t<Mpsc, Direction::Producer>>>);
static_assert(
    proto::is_dual_v<default_proto_for_t<Spsc, Direction::Producer>, default_proto_for_t<Spsc, Direction::Consumer>>);
static_assert(
    proto::is_dual_v<default_proto_for_t<Mpsc, Direction::Producer>, default_proto_for_t<Mpsc, Direction::Consumer>>);

static_assert(IsBridgeableDirection<Spsc, Direction::Producer>);
static_assert(IsBridgeableDirection<Spsc, Direction::Consumer>);
static_assert(IsBridgeableDirection<Mpsc, Direction::Producer>);
static_assert(IsBridgeableDirection<Mpsc, Direction::Consumer>);

static_assert(!IsBridgeableDirection<int, Direction::Producer>);
static_assert(!IsBridgeableDirection<int, Direction::Consumer>);
static_assert(!IsBridgeableDirection<Spsc const, Direction::Producer>, "a cv-qualified channel has no row");

// A type that names the two ends and a value type is still no channel of
// the table.
struct LooksLikeAChannel {
    using value_type = int;
    using ProducerEnd = typename Spsc::ProducerEnd;
    using ConsumerEnd = typename Spsc::ConsumerEnd;
};
static_assert(!IsBridgeableDirection<LooksLikeAChannel, Direction::Producer>);

// Each end and each frame handle of an end is a pole of its channel, and the
// table of the bridge gives each pole the protocol of its direction and the
// protocol of the other direction as its peer.  A mint of one end refuses a
// pole, and the pole door admits it.
static_assert(proto::ChannelPole<typename Spsc::ProducerEnd> && proto::ChannelPole<typename Spsc::ConsumerEnd>);
static_assert(proto::ChannelPole<SpscProducer> && proto::ChannelPole<MpscConsumer>);
static_assert(!proto::PlainSessionResource<SpscProducer>);
static_assert(!proto::PlainSessionResource<MpscConsumer>);
static_assert(std::is_same_v<proto::pole_protocol_t<SpscProducer>, default_proto_for_t<Spsc, Direction::Producer>>);
static_assert(
    std::is_same_v<proto::pole_peer_protocol_t<SpscProducer>, default_proto_for_t<Spsc, Direction::Consumer>>);
static_assert(std::is_same_v<proto::pole_protocol_t<MpscConsumer>, default_proto_for_t<Mpsc, Direction::Consumer>>);
static_assert(
    std::is_same_v<proto::pole_peer_protocol_t<MpscConsumer>, default_proto_for_t<Mpsc, Direction::Producer>>);
static_assert(
    std::is_same_v<proto::pole_protocol_t<typename Spsc::ProducerEnd>, default_proto_for_t<Spsc, Direction::Producer>>);
static_assert(proto::ChannelSidesAgree<proto::pole_protocol_t<SpscProducer>, proto::pole_peer_protocol_t<SpscProducer>,
                                       SpscProducer, SpscProducer>);

// The mint takes a frame handle of the end of its direction, by move.
using FgCtx = ::foundation::effects::ExecCtx<::foundation::effects::ctx_cap::Fg, ::foundation::effects::Row<>>;
static_assert(CtxFitsSubstrateSessionHandle<Spsc, Direction::Producer, FgCtx, SpscProducer>);
static_assert(!CtxFitsSubstrateSessionHandle<Spsc, Direction::Consumer, FgCtx, SpscProducer>);
static_assert(!CtxFitsSubstrateSessionHandle<Spsc, Direction::Producer, FgCtx, SpscProducer&>);

// A class that names no bridged channel is no pole of the table.
static_assert(!proto::HasPoleTable<LooksLikeAChannel>);
static_assert(!proto::HasPoleTable<int>);

}  // namespace detail::substrate_session_bridge_self_test

}  // namespace fixy::channels
