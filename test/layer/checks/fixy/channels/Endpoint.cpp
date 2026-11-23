// The compile-time checks of fixy/channels/Endpoint.h.

#include <fixy/channels/Endpoint.h>

namespace fixy::channels {

namespace detail::endpoint_self_test {

namespace proto = ::fixy::session;

using endpoint_witness::UserTag;
using endpoint_witness::UserBrand;
using SmallSpsc =
    ::fixy::concurrent::PermissionedChannel<::fixy::concurrent::ChannelForm::wire, int, 64, UserTag, UserBrand>;
using SmallMpsc =
    ::fixy::concurrent::PermissionedChannel<::fixy::concurrent::ChannelForm::merge, int, 64, UserTag, UserBrand>;
using endpoint_witness::FgCtx;
// A scope brand that the checks name.  An endpoint holds a frame handle of the
// end of its direction, on the scope brand of its frame.
struct CheckScope {};
using SpscProducer = ::fixy::concurrent::ProducerHandle<SmallSpsc, CheckScope>;
using SpscConsumer = ::fixy::concurrent::ConsumerHandle<SmallSpsc, CheckScope>;
using MpscProducer = ::fixy::concurrent::ProducerHandle<SmallMpsc, CheckScope>;
using ProdEp = Endpoint<SmallSpsc, Direction::Producer, FgCtx, SpscProducer>;
using ConsEp = Endpoint<SmallSpsc, Direction::Consumer, FgCtx, SpscConsumer>;
using MpscProdEp = Endpoint<SmallMpsc, Direction::Producer, FgCtx, MpscProducer>;

static_assert(std::is_same_v<typename ProdEp::handle_type, SpscProducer>);
static_assert(std::is_same_v<typename ConsEp::handle_type, SpscConsumer>);
static_assert(std::is_same_v<typename MpscProdEp::handle_type, MpscProducer>);

// An endpoint takes a frame handle of the end of its direction, and no end
// and no handle of the other direction.
template <class Handle>
concept NamesProducerEndpointOver = requires { typename Endpoint<SmallSpsc, Direction::Producer, FgCtx, Handle>; };
static_assert(NamesProducerEndpointOver<SpscProducer>);
static_assert(!NamesProducerEndpointOver<SpscConsumer>);
static_assert(!NamesProducerEndpointOver<typename SmallSpsc::ProducerEnd>);
static_assert(CtxFitsEndpointHandle<SmallSpsc, Direction::Producer, FgCtx, SpscProducer>
              && !CtxFitsEndpointHandle<SmallSpsc, Direction::Producer, FgCtx, SpscProducer&>);
static_assert(std::is_same_v<typename ProdEp::value_type, int>);
static_assert(std::is_same_v<typename ProdEp::user_tag, UserTag>);
static_assert(std::is_same_v<typename ProdEp::ctx_type, FgCtx>);
static_assert(std::is_same_v<typename ProdEp::proto_type, proto::Loop<proto::Send<int, proto::Continue>>>);

// Head line, tail line and the cell line, as the handle states.
static_assert(ProdEp::per_call_working_set == 192);

static_assert(!std::is_copy_constructible_v<ProdEp>);
static_assert(!std::is_copy_assignable_v<ProdEp>);
static_assert(std::is_move_constructible_v<ProdEp>);
static_assert(!std::is_move_assignable_v<ProdEp>);
static_assert(std::is_nothrow_move_constructible_v<ProdEp>);

static_assert(sizeof(ProdEp) == sizeof(void*), "Endpoint must collapse to pointer-size — Ctx EBO-collapse is "
                                               "load-bearing for the zero-runtime-cost claim.");
static_assert(sizeof(ConsEp) == sizeof(void*));

}  // namespace detail::endpoint_self_test

}  // namespace fixy::channels
