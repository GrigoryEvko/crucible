// The compile-time checks of fixy/channels/StageEndpointBridge.h.

#include <fixy/channels/StageEndpointBridge.h>

namespace fixy::channels {

namespace detail::stage_endpoint_bridge_self_test {

namespace concurrent = ::fixy::concurrent;

// The tags, the channels and the endpoints that the checks name.  No code
// builds a channel of them.
struct UTag1 {};
struct UTag2 {};
struct UBrand {};

using FgCtx = ::foundation::effects::ExecCtx<::foundation::effects::ctx_cap::Fg, ::foundation::effects::Row<>>;
using Ch1 = concurrent::PermissionedChannel<concurrent::ChannelForm::wire, int, 64, UTag1, UBrand>;
using Ch2 = concurrent::PermissionedChannel<concurrent::ChannelForm::wire, int, 64, UTag2, UBrand>;

// A scope brand that the checks name, and the frame handles on it.  No frame
// door gives a handle on it.
struct CheckScope {};
template <class Channel>
using InOf = concurrent::ConsumerHandle<Channel, CheckScope>;
template <class Channel>
using OutOf = concurrent::ProducerHandle<Channel, CheckScope>;

using ConsEp = Endpoint<Ch1, Direction::Consumer, FgCtx, InOf<Ch1>>;
using ProdEp = Endpoint<Ch2, Direction::Producer, FgCtx, OutOf<Ch2>>;

static_assert(IsEndpoint<ConsEp>);
static_assert(IsEndpoint<ProdEp>);
static_assert(!IsEndpoint<int>);

static_assert(IsConsumerEndpoint<ConsEp>);
static_assert(!IsConsumerEndpoint<ProdEp>);
static_assert(!IsConsumerEndpoint<int>);

static_assert(IsProducerEndpoint<ProdEp>);
static_assert(!IsProducerEndpoint<ConsEp>);
static_assert(!IsProducerEndpoint<int>);

static_assert(IsMovedEndpoint<ConsEp>);
static_assert(IsMovedEndpoint<ConsEp&&>);
static_assert(!IsMovedEndpoint<ConsEp&>);

inline void int_stage_body(InOf<Ch1>&&, OutOf<Ch2>&&) noexcept {}

static_assert(concurrent::PipelineStage<&int_stage_body>);

static_assert(StageHandlesMatchEndpoints<&int_stage_body, ConsEp, ProdEp>);

static_assert(CtxFitsStageFromEndpoints<&int_stage_body, FgCtx, ConsEp, ProdEp>);

static_assert(!CtxFitsStageFromEndpoints<&int_stage_body, FgCtx, int, ProdEp>);
static_assert(!CtxFitsStageFromEndpoints<&int_stage_body, FgCtx, ConsEp, int>);
static_assert(!CtxFitsStageFromEndpoints<&int_stage_body, FgCtx, ProdEp, ConsEp>);
static_assert(!CtxFitsStageFromEndpoints<&int_stage_body, int, ConsEp, ProdEp>);
static_assert(!CtxFitsStageFromEndpoints<&int_stage_body, FgCtx, ConsEp&, ProdEp>);

// The payload axis is pinned here as well as in the negative-compile
// fixtures, so a refactor that loses payload-type discrimination breaks
// this check file on its own rather than only the tests.

struct UTagFloat {};
using ChFloat = concurrent::PermissionedChannel<concurrent::ChannelForm::wire, float, 64, UTagFloat, UBrand>;
using FloatConsEp = Endpoint<ChFloat, Direction::Consumer, FgCtx, InOf<ChFloat>>;
using FloatProdEp = Endpoint<ChFloat, Direction::Producer, FgCtx, OutOf<ChFloat>>;

// Direction, body shape and context all agree in the four rejections below.
// Only the payload type differs.
static_assert(IsConsumerEndpoint<FloatConsEp>);
static_assert(IsProducerEndpoint<FloatProdEp>);
static_assert(!StageHandlesMatchEndpoints<&int_stage_body, FloatConsEp, ProdEp>);
static_assert(!StageHandlesMatchEndpoints<&int_stage_body, ConsEp, FloatProdEp>);
static_assert(!CtxFitsStageFromEndpoints<&int_stage_body, FgCtx, FloatConsEp, ProdEp>);
static_assert(!CtxFitsStageFromEndpoints<&int_stage_body, FgCtx, ConsEp, FloatProdEp>);

// A positive control, so that a gate which rejects everything cannot pass.
static_assert(StageHandlesMatchEndpoints<&int_stage_body, ConsEp, ProdEp>);

// A stage can feed an MPSC channel.  Its producer handle takes the value
// by reference to const, which is the producer pole, so the body is a
// stage and the endpoint mint takes the MPSC producer endpoint.
struct UTagMpsc {};
using ChMpsc = concurrent::PermissionedChannel<concurrent::ChannelForm::merge, int, 64, UTagMpsc, UBrand>;
using MpscProdEp = Endpoint<ChMpsc, Direction::Producer, FgCtx, OutOf<ChMpsc>>;

inline void feed_mpsc_body(InOf<Ch1>&&, OutOf<ChMpsc>&&) noexcept {}

static_assert(concurrent::is_producer_handle_v<OutOf<ChMpsc>>);
static_assert(concurrent::PipelineStage<&feed_mpsc_body>);
static_assert(CtxFitsStageFromEndpoints<&feed_mpsc_body, FgCtx, ConsEp, MpscProdEp>);

inline void fan_in_body(InOf<Ch1>&&, InOf<Ch1>&&, OutOf<Ch2>&&) noexcept {}
inline void fan_out_body(InOf<Ch1>&&, OutOf<Ch2>&&, OutOf<Ch2>&&) noexcept {}

static_assert(concurrent::VariadicPipelineStage<&fan_in_body>);
static_assert(!concurrent::PipelineStage<&fan_in_body>);
static_assert(StageHandlesMatchEndpointsExtended<&fan_in_body, EndpointPack<ConsEp, ConsEp>, EndpointPack<ProdEp>>);
static_assert(!StageHandlesMatchEndpointsExtended<&fan_in_body, EndpointPack<ConsEp>, EndpointPack<ProdEp>>);
static_assert(
    !StageHandlesMatchEndpointsExtended<&fan_in_body, EndpointPack<ConsEp, ConsEp, ConsEp>, EndpointPack<ProdEp>>);
static_assert(!StageHandlesMatchEndpointsExtended<&fan_in_body, EndpointPack<ConsEp, int>, EndpointPack<ProdEp>>);
static_assert(CtxFitsMpmcStageFromEndpoints<&fan_in_body, FgCtx, ConsEp, ConsEp, ProdEp>);
static_assert(!CtxFitsMpmcStageFromEndpoints<&fan_in_body, FgCtx, ConsEp&, ConsEp, ProdEp>);

static_assert(concurrent::VariadicPipelineStage<&fan_out_body>);
static_assert(!concurrent::PipelineStage<&fan_out_body>);
static_assert(StageHandlesMatchEndpointsExtended<&fan_out_body, EndpointPack<ConsEp>, EndpointPack<ProdEp, ProdEp>>);
static_assert(!StageHandlesMatchEndpointsExtended<&fan_out_body, EndpointPack<ConsEp, ProdEp>, EndpointPack<ProdEp>>);
static_assert(
    !StageHandlesMatchEndpointsExtended<&fan_out_body, EndpointPack<ConsEp>, EndpointPack<ProdEp, ProdEp, ProdEp>>);

}  // namespace detail::stage_endpoint_bridge_self_test

}  // namespace fixy::channels
