// The reason that the synchronous relation of fixy/session/Subtype.h gives
// for a refusal, and the relations derived from it: the strict relation,
// the equivalence, the chains, compatibility and the concepts.

#include "session_subtype.h"

#include <type_traits>

namespace test_session_subtype_types {

// ── The reason ───────────────────────────────────────────────────────

using tr::mismatch;

static_assert(std::is_same_v<s::subtype_reason_t<End, End>, s::SubtypeOk>);
static_assert(std::is_same_v<s::subtype_reason_t<Send<int, End>, Send<long, End>>,
                             s::SubtypeRejection<mismatch::payload, int, long>>);
static_assert(std::is_same_v<s::subtype_reason_t<Recv<int, End>, Recv<long, End>>,
                             s::SubtypeRejection<mismatch::payload, long, int>>,
              "for a Recv the pair reads supplied against expected");
static_assert(std::is_same_v<s::subtype_reason_t<Send<int, End>, Recv<int, End>>,
                             s::SubtypeRejection<mismatch::shape, Send<int, End>, Recv<int, End>>>);
static_assert(std::is_same_v<s::subtype_reason_t<Select<End>, Offer<End>>,
                             s::SubtypeRejection<mismatch::shape, Select<End>, Offer<End>>>);
using SelectTooManyT = Select<Send<PingReq, End>, End>;
using SelectTooManyU = Select<Send<PingReq, End>>;
static_assert(std::is_same_v<s::subtype_reason_t<SelectTooManyT, SelectTooManyU>,
                             s::SubtypeRejection<mismatch::branch_count, SelectTooManyT, SelectTooManyU>>);
using OfferTooFewT = Offer<Recv<PingReq, End>>;
using OfferTooFewU = Offer<Recv<PingReq, End>, End>;
static_assert(std::is_same_v<s::subtype_reason_t<OfferTooFewT, OfferTooFewU>,
                             s::SubtypeRejection<mismatch::branch_count, OfferTooFewT, OfferTooFewU>>);
using NestedT = Loop<Send<int, Recv<int, Continue>>>;
using NestedU = Loop<Send<int, Recv<long, Continue>>>;
static_assert(std::is_same_v<s::subtype_reason_t<NestedT, NestedU>, s::SubtypeRejection<mismatch::payload, long, int>>,
              "the innermost cause is reported, not the outer loop");
static_assert(
    std::is_same_v<s::subtype_reason_t<Select<Send<int, End>, Send<int, End>>, Select<Send<int, End>, Send<long, End>>>,
                   s::SubtypeRejection<mismatch::payload, int, long>>);
static_assert(
    std::is_same_v<s::subtype_reason_t<Continue, End>, s::SubtypeRejection<mismatch::ill_formed, Continue, void>>);
static_assert(
    std::is_same_v<s::subtype_reason_t<End, Loop<End>>, s::SubtypeRejection<mismatch::ill_formed, void, Loop<End>>>);
static_assert(std::is_same_v<s::subtype_reason_t<Offer<Sender<Alice>, End>, Offer<End>>,
                             s::SubtypeRejection<mismatch::annotation, Offer<Sender<Alice>, End>, Offer<End>>>);
static_assert(s::SubtypeRejection<mismatch::payload, int, long>::description == tr::mismatch_name(mismatch::payload));

// One walk gives both the verdict and the reason, so they cannot
// disagree: the reason is SubtypeOk exactly when the verdict holds.
template <class T, class U>
inline constexpr bool reason_agrees_v =
    std::is_same_v<s::subtype_reason_t<T, U>, s::SubtypeOk> == s::is_subtype_sync_v<T, U>;
static_assert(reason_agrees_v<End, End> && reason_agrees_v<NvSendInt, AmdSendInt> && reason_agrees_v<NestedT, NestedU>
              && reason_agrees_v<SelectTooManyU, SelectTooManyT> && reason_agrees_v<Continue, Continue>);

// ── Derived relations ────────────────────────────────────────────────

static_assert(!s::is_strict_subtype_sync_v<End, End> && !s::is_strict_subtype_sync_v<DS1, DS1>);
static_assert(s::is_strict_subtype_sync_v<DS1, DS2> && !s::is_strict_subtype_sync_v<DS2, DS1>);
static_assert(s::is_strict_subtype_sync_v<DO1, DO2> && !s::is_strict_subtype_sync_v<DO2, DO1>);
static_assert(!s::is_strict_subtype_sync_v<Send<int, End>, Recv<int, End>>);
static_assert(s::equivalent_sync_v<End, End> && s::equivalent_sync_v<DS1, DS1>);
static_assert(!s::equivalent_sync_v<DS1, DS2> && !s::equivalent_sync_v<DO1, DO2>);

using TSelectT = Select<Send<PingReq, End>>;
using TSelectU = Select<Send<PingReq, End>, Send<StopReq, End>>;
using TSelectV = Select<Send<PingReq, End>, Send<StopReq, End>, Recv<PingReq, End>>;
using TOfferW = Offer<Recv<PingReq, End>, Recv<StopReq, End>, Send<PingReq, End>>;
using TOfferX = Offer<Recv<PingReq, End>, Recv<StopReq, End>>;
using TOfferY = Offer<Recv<PingReq, End>>;
static_assert(s::subtype_chain_v<TSelectT, TSelectU, TSelectV> && s::is_subtype_sync_v<TSelectT, TSelectV>);
static_assert(s::subtype_chain_v<TOfferW, TOfferX, TOfferY> && s::is_subtype_sync_v<TOfferW, TOfferY>);
static_assert(s::subtype_chain_v<End> && s::subtype_chain_v<End, End> && s::subtype_chain_v<>);
static_assert(!s::subtype_chain_v<TSelectU, TSelectT, TSelectV>);

namespace client_server {
using ReqRespClient = Loop<Send<Req, Recv<Resp, Continue>>>;
using ReqRespServer = Loop<Recv<Req, Send<Resp, Continue>>>;
static_assert(std::is_same_v<s::dual_of_t<ReqRespServer>, ReqRespClient>);
static_assert(s::CompatibleClient<ReqRespClient, ReqRespServer> && s::CompatibleServer<ReqRespServer, ReqRespClient>);
static_assert(!s::CompatibleClient<ReqRespServer, ReqRespServer> && !s::CompatibleServer<ReqRespClient, ReqRespClient>);

consteval bool asserts_hold() {
    s::assert_subtype_sync<DS1, DS2>();
    s::assert_equivalent_sync<DS1, DS1>();
    s::assert_compatible_client<ReqRespClient, ReqRespServer>();
    s::assert_compatible_server<ReqRespServer, ReqRespClient>();
    return true;
}
static_assert(asserts_hold());
}  // namespace client_server

template <class T, class U>
    requires s::SubtypeSync<T, U>
consteval bool requires_subtype() {
    return true;
}
static_assert(requires_subtype<DS1, DS2>());

template <class T, class U>
    requires s::StrictSubtypeSync<T, U>
consteval bool requires_strict_subtype() {
    return true;
}
static_assert(requires_strict_subtype<DS1, DS2>());

// Closure under duality, one witness per combinator.
static_assert(s::is_subtype_sync_v<s::dual_of_t<DS2>, s::dual_of_t<DS1>>);
static_assert(s::is_subtype_sync_v<s::dual_of_t<DO2>, s::dual_of_t<DO1>>);
using DLoopS1 = Loop<Send<int, Select<Send<PingReq, Continue>, Send<StopReq, End>>>>;
using DLoopS2 = Loop<Send<int, Select<Send<PingReq, Continue>, Send<StopReq, End>, Send<int, End>>>>;
static_assert(s::is_subtype_sync_v<DLoopS1, DLoopS2>
              && s::is_subtype_sync_v<s::dual_of_t<DLoopS2>, s::dual_of_t<DLoopS1>>);

// Exit preservation is not closed under duality.  The loop that never
// picks its exit does not refine the loop that can, and the dual pair
// refines, because a receiver that never ends keeps no exit.
// Compatibility asks for both directions, so it refuses the pair from
// either side.
using NeverStops = Loop<Send<int, Select<Send<PingReq, Continue>>>>;
static_assert(s::subtype_mismatch_v<NeverStops, DLoopS1> == tr::mismatch::loses_termination);
static_assert(s::is_subtype_sync_v<s::dual_of_t<DLoopS1>, s::dual_of_t<NeverStops>>);
static_assert(!s::CompatibleServer<NeverStops, s::dual_of_t<DLoopS1>>
              && !s::CompatibleClient<s::dual_of_t<DLoopS1>, NeverStops>);

// The asynchronous relation reads exit preservation on its derivation.
// The eager loop sends before it receives and never stops.  The bounded
// search alone proves the pair, and the exit check refuses it.  The
// eager loop that keeps the stop holds.
using PatientLoop = Loop<Recv<Req, Select<Send<Resp, Continue>, Send<StopReq, End>>>>;
using EagerEndless = Loop<Select<Send<Resp, Recv<Req, Continue>>>>;
using EagerStopping = Loop<Select<Send<Resp, Recv<Req, Continue>>, Send<StopReq, Recv<Req, End>>>>;
static_assert(s::detail::async::bounded(^^EagerEndless, ^^PatientLoop, 2, false));
static_assert(!s::is_subtype_async_v<EagerEndless, PatientLoop, Slots<2>>);
static_assert(s::is_subtype_async_v<EagerStopping, PatientLoop, Slots<2>>);

}  // namespace test_session_subtype_types
