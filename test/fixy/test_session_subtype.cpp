// Session subtyping: the synchronous relation, its reason, the derived
// relations, the payload order and the bounded asynchronous relation of
// fixy/session/Subtype.h.  The last part generates protocols from a
// fixed seed and checks the laws on each: reflexivity, transitivity,
// closure under duality, the involution of duality, and that the
// asynchronous relation holds each synchronous pair.

#include <fixy/session/Projection.h>
#include <fixy/session/Subtype.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <meta>
#include <string_view>
#include <type_traits>
#include <vector>

namespace s = ::fixy::session;
namespace tr = ::foundation::algebra::transition;
namespace tags = ::fixy::tags;
using ::foundation::algebra::lattices::Tolerance;

namespace {

struct Alice {};
struct Bob {};
struct PingReq {};
struct StopReq {};
struct Req {};
struct Resp {};
struct CloseCmd {};
struct Job {};

using s::Continue;
using s::End;
using s::Loop;
using s::Offer;
using s::Recv;
using s::Select;
using s::Send;
using s::Sender;
using s::VendorPinned;
using s::VendorBackend;

// ── Reflexivity on every combinator ──────────────────────────────────

static_assert(s::is_subtype_sync_v<End, End>);
static_assert(s::is_subtype_sync_v<Send<int, End>, Send<int, End>>);
static_assert(s::is_subtype_sync_v<Recv<int, End>, Recv<int, End>>);
static_assert(s::is_subtype_sync_v<Loop<Send<int, Continue>>, Loop<Send<int, Continue>>>);
static_assert(s::is_subtype_sync_v<Select<Send<int, End>, Recv<bool, End>>, Select<Send<int, End>, Recv<bool, End>>>);
static_assert(s::is_subtype_sync_v<Offer<Recv<int, End>, Send<bool, End>>, Offer<Recv<int, End>, Send<bool, End>>>);
static_assert(s::is_subtype_sync_v<Offer<Sender<Alice>, Recv<int, End>>, Offer<Sender<Alice>, Recv<int, End>>>,
              "the old relation was not reflexive on an Offer with a note");
static_assert(!s::is_subtype_sync_v<Offer<Sender<Alice>>, Offer<Sender<Alice>>>,
              "an Offer with a note and no branch is an empty choice, which is not well-formed");
static_assert(s::is_subtype_sync_v<Loop<Loop<Send<int, Continue>>>, Loop<Loop<Send<int, Continue>>>>);

using NvSendInt = VendorPinned<VendorBackend::NV, Send<int, End>>;
using AmdSendInt = VendorPinned<VendorBackend::AMD, Send<int, End>>;
using PortableSendInt = VendorPinned<VendorBackend::Portable, Send<int, End>>;
static_assert(s::is_subtype_sync_v<NvSendInt, NvSendInt>);
static_assert(!s::is_subtype_sync_v<NvSendInt, AmdSendInt> && !s::is_subtype_sync_v<AmdSendInt, NvSendInt>);
static_assert(!s::is_subtype_sync_v<PortableSendInt, NvSendInt> && !s::is_subtype_sync_v<NvSendInt, PortableSendInt>,
              "the vendor is invariant: an order would not be closed under duality");
static_assert(s::subtype_mismatch_v<PortableSendInt, NvSendInt> == tr::mismatch::value);
static_assert(!s::is_subtype_sync_v<VendorPinned<VendorBackend::None, End>, VendorPinned<VendorBackend::None, End>>,
              "VendorBackend::None names no kernel, so the protocol is not well-formed");

// ── Shapes that do not relate ────────────────────────────────────────

static_assert(!s::is_subtype_sync_v<Send<int, End>, Recv<int, End>>);
static_assert(!s::is_subtype_sync_v<Recv<int, End>, Send<int, End>>);
static_assert(!s::is_subtype_sync_v<End, Send<int, End>> && !s::is_subtype_sync_v<Send<int, End>, End>);
static_assert(!s::is_subtype_sync_v<Select<End>, Offer<End>>);
static_assert(!s::is_subtype_sync_v<Loop<Send<int, Continue>>, End>);
static_assert(!s::is_subtype_sync_v<End, Loop<Send<int, Continue>>>);
static_assert(!s::is_subtype_sync_v<Offer<Sender<Alice>, End>, Offer<End>>, "the note is compared");
static_assert(!s::is_subtype_sync_v<Offer<Sender<Alice>, End>, Offer<Sender<Bob>, End>>);

// An operand that is not well-formed relates to nothing, itself
// included.  The old relation held Continue against Continue.
static_assert(!s::is_subtype_sync_v<Continue, Continue>);
static_assert(s::subtype_mismatch_v<Continue, End> == tr::mismatch::ill_formed);
static_assert(!s::is_subtype_sync_v<Loop<End>, Loop<End>>);
static_assert(!s::is_subtype_sync_v<Loop<Continue>, Loop<Continue>>, "an unguarded loop is not well-formed");

// ── Width, position and recursion ────────────────────────────────────

static_assert(s::is_subtype_sync_v<Send<int, Select<Send<PingReq, End>>>,
                                   Send<int, Select<Send<PingReq, End>, Send<StopReq, End>>>>);
static_assert(s::is_subtype_sync_v<Recv<int, Select<Send<PingReq, End>>>,
                                   Recv<int, Select<Send<PingReq, End>, Send<StopReq, End>>>>);
// An empty choice is not well-formed.  Under the branch rule Select<>
// refines every Select, and a substitute of that type never sends, so
// the session deadlocks.
static_assert(!s::is_subtype_sync_v<Select<>, Select<Send<PingReq, End>>>);
static_assert(s::subtype_mismatch_v<Select<>, Select<Send<PingReq, End>>> == tr::mismatch::ill_formed);
static_assert(!s::is_subtype_async_v<Select<>, Select<Send<PingReq, End>>, 4>);
static_assert(!s::is_subtype_sync_v<Send<int, Select<>>, Send<int, Select<Send<PingReq, End>>>>,
              "an empty choice below the top is refused too");
static_assert(!s::is_subtype_sync_v<Select<Send<PingReq, End>, Send<StopReq, End>>, Select<Send<PingReq, End>>>);
static_assert(s::is_subtype_sync_v<Offer<Recv<PingReq, End>, Recv<StopReq, End>, End>,
                                   Offer<Recv<PingReq, End>, Recv<StopReq, End>>>);
static_assert(!s::is_subtype_sync_v<Offer<Recv<PingReq, End>>, Offer<Recv<PingReq, End>, Recv<StopReq, End>>>);
static_assert(!s::is_subtype_sync_v<Offer<>, Offer<Recv<PingReq, End>>>);
static_assert(!s::is_subtype_sync_v<Offer<>, Offer<>>);
static_assert(s::subtype_mismatch_v<Offer<>, Offer<>> == tr::mismatch::ill_formed);
static_assert(s::is_subtype_sync_v<Loop<Select<Send<PingReq, Continue>>>,
                                   Loop<Select<Send<PingReq, Continue>, Send<StopReq, End>>>>);
static_assert(!s::is_subtype_sync_v<Select<Send<PingReq, End>, Send<StopReq, End>>,
                                    Select<Send<StopReq, End>, Send<PingReq, End>>>,
              "the position of a label branch is the label on the wire");

// ── Branches that are no label ───────────────────────────────────────
//
// A crash branch has no position on the wire: the endpoint enters it
// when it detects the crash.  So the subtype can add a message branch
// before the crash branch of its Offer (rule Sub-&, LMCS 2025, Def. 4.4),
// and the crash branches of the two sides pair by the payload they
// receive, in any order.

using AliceCrash = Recv<s::Crash<Alice>, End>;
using BobCrash = Recv<s::Crash<Bob>, End>;
static_assert(s::is_subtype_sync_v<Offer<Recv<PingReq, End>, Recv<StopReq, End>, AliceCrash>,
                                   Offer<Recv<PingReq, End>, AliceCrash>>,
              "a message branch added before the crash branch");
static_assert(s::is_subtype_sync_v<Offer<Recv<PingReq, End>, BobCrash, AliceCrash>,
                                   Offer<Recv<PingReq, End>, AliceCrash, BobCrash>>,
              "crash branches pair by payload, not by position");
static_assert(s::subtype_mismatch_v<Offer<Recv<PingReq, End>>, Offer<Recv<PingReq, End>, AliceCrash>>
                  == tr::mismatch::missing_non_label_branch,
              "the subtype must handle each crash that the supertype handles");
static_assert(s::subtype_mismatch_v<Offer<Recv<PingReq, End>, AliceCrash>, Offer<Recv<PingReq, End>>>
                  == tr::mismatch::non_label_branch,
              "the subtype may not add a crash branch");
static_assert(!s::is_subtype_sync_v<Offer<Recv<StopReq, End>, Recv<PingReq, End>, AliceCrash>,
                                    Offer<Recv<PingReq, End>, AliceCrash>>,
              "a message branch keeps its position");
static_assert(!s::is_well_formed_v<Offer<AliceCrash, Recv<PingReq, End>>>,
              "a message branch after a crash branch has no wire label");
static_assert(!s::is_well_formed_v<Offer<Recv<PingReq, End>, AliceCrash, Recv<s::Crash<Alice>, Send<int, End>>>>,
              "two crash branches for one peer");
static_assert(!s::is_well_formed_v<Offer<AliceCrash>>, "a choice of crash branches only is empty");

// ── Labels that a payload names ──────────────────────────────────────
//
// A PeerMsg names a peer and a label.  Two branches of one choice that
// name the same label are not well-formed, whatever their payloads.

static_assert(!s::is_well_formed_v<Select<Send<s::PeerMsg<Bob, PingReq, int>, End>,
                                          Send<s::PeerMsg<Bob, PingReq, int>, Send<int, End>>>>);
static_assert(!s::is_well_formed_v<Select<Send<s::PeerMsg<Bob, PingReq, int>, End>,
                                          Send<s::PeerMsg<Bob, PingReq, bool>, End>>>,
              "the payload is not part of the label");
static_assert(s::is_well_formed_v<Select<Send<s::PeerMsg<Bob, PingReq, int>, End>,
                                         Send<s::PeerMsg<Alice, PingReq, int>, End>>>,
              "the peer is part of the label");
static_assert(!s::is_well_formed_v<Offer<Sender<Bob>, Recv<s::PeerMsg<Bob, PingReq, int>, End>,
                                         Recv<s::PeerMsg<Bob, PingReq, int>, End>>>);

// The relation is on the unfoldings, which the old lockstep walk could
// not see.
static_assert(s::is_subtype_sync_v<Loop<Send<int, Continue>>, Send<int, Loop<Send<int, Continue>>>>);
static_assert(s::is_subtype_sync_v<Send<int, Loop<Send<int, Continue>>>, Loop<Send<int, Continue>>>);
static_assert(s::equivalent_sync_v<Loop<Send<int, Send<int, Continue>>>, Loop<Send<int, Continue>>>);
static_assert(!s::is_subtype_sync_v<Loop<Send<int, Continue>>, Send<int, End>>);
static_assert(!s::is_subtype_sync_v<Send<int, End>, Loop<Send<int, Continue>>>);

namespace evolution {
using ServerV1 =
    Loop<Offer<Recv<Req, Send<Resp, Continue>>, Recv<CloseCmd, End>, Recv<PingReq, Send<PingReq, Continue>>>>;
using ServerV2 = Loop<Offer<Recv<Req, Send<Resp, Continue>>, Recv<CloseCmd, End>, Recv<PingReq, Send<PingReq, Continue>>,
                            Recv<StopReq, Send<Resp, End>>>>;
static_assert(s::is_subtype_sync_v<ServerV2, ServerV1> && !s::is_subtype_sync_v<ServerV1, ServerV2>);

consteval bool evolution_holds() {
    s::check_protocol_evolution<ServerV1, ServerV2>();
    return true;
}
static_assert(evolution_holds());
}  // namespace evolution

namespace mpmc {
using ProducerFull = Loop<Select<Send<Job, Continue>, Send<Job, Continue>, End>>;
using ProducerNarrow = Loop<Select<Send<Job, Continue>>>;
static_assert(s::is_subtype_sync_v<ProducerNarrow, ProducerFull> && !s::is_subtype_sync_v<ProducerFull, ProducerNarrow>);
}  // namespace mpmc

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
static_assert(std::is_same_v<s::subtype_reason_t<Select<Send<int, End>, Send<int, End>>,
                                                 Select<Send<int, End>, Send<long, End>>>,
                             s::SubtypeRejection<mismatch::payload, int, long>>);
static_assert(std::is_same_v<s::subtype_reason_t<Continue, End>, s::SubtypeRejection<mismatch::ill_formed, Continue, void>>);
static_assert(std::is_same_v<s::subtype_reason_t<End, Loop<End>>, s::SubtypeRejection<mismatch::ill_formed, void, Loop<End>>>);
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

using DS1 = Select<Send<PingReq, End>>;
using DS2 = Select<Send<PingReq, End>, Send<StopReq, End>>;
using DO1 = Offer<Recv<PingReq, End>, Recv<StopReq, End>>;
using DO2 = Offer<Recv<PingReq, End>>;

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
using DLoopS1 = Loop<Send<int, Select<Send<PingReq, Continue>>>>;
using DLoopS2 = Loop<Send<int, Select<Send<PingReq, Continue>, Send<StopReq, End>>>>;
static_assert(s::is_subtype_sync_v<DLoopS1, DLoopS2> && s::is_subtype_sync_v<s::dual_of_t<DLoopS2>, s::dual_of_t<DLoopS1>>);

// ── The payload order ────────────────────────────────────────────────

struct DispatchRequest {
    int op_id = 0;
};
struct MemoryPlanByte {
    unsigned char value = 0;
};
struct TensorTile {
    float value[4] = {};
};

using ::fixy::Refined;
using ::fixy::SealedRefined;
using ::fixy::Tagged;

static_assert(s::is_payload_subsort_v<Refined<::fixy::positive, int>, int>);
static_assert(s::is_payload_subsort_v<Refined<::fixy::non_zero, int>, int>);
static_assert(s::is_payload_subsort_v<Refined<::fixy::bounded_above<1024>, int>, int>);
static_assert(!s::is_payload_subsort_v<int, Refined<::fixy::positive, int>>, "a value does not acquire a guarantee");
static_assert(!s::is_payload_subsort_v<SealedRefined<::fixy::positive, int>, int>,
              "a sealed refinement has no door that returns the value");
static_assert(s::is_payload_subsort_v<SealedRefined<::fixy::positive, int>, SealedRefined<::fixy::non_negative, int>>);
static_assert(!s::is_payload_subsort_v<Refined<::fixy::positive, int>, SealedRefined<::fixy::non_negative, int>>);

static_assert(s::is_payload_subsort_v<Tagged<DispatchRequest, tags::source::Sanitized>, DispatchRequest>);
static_assert(s::is_payload_subsort_v<Tagged<DispatchRequest, tags::source::FromInternal>, DispatchRequest>);
static_assert(s::is_payload_subsort_v<Tagged<DispatchRequest, tags::source::FromConfig>, DispatchRequest>);
static_assert(s::is_payload_subsort_v<Tagged<DispatchRequest, tags::source::FromDb>, DispatchRequest>);
static_assert(s::is_payload_subsort_v<Tagged<MemoryPlanByte, tags::source::Durable>, MemoryPlanByte>);
static_assert(s::is_payload_subsort_v<Tagged<MemoryPlanByte, tags::source::Computed>, MemoryPlanByte>);
static_assert(s::is_payload_subsort_v<Tagged<DispatchRequest, tags::vessel_trust::Validated>, DispatchRequest>);
static_assert(!s::is_payload_subsort_v<Tagged<DispatchRequest, tags::source::External>, DispatchRequest>);
static_assert(!s::is_payload_subsort_v<Tagged<DispatchRequest, tags::source::FromUser>, DispatchRequest>);
static_assert(!s::is_payload_subsort_v<Tagged<DispatchRequest, tags::vessel_trust::FromPytorch>, DispatchRequest>);
static_assert(!s::is_payload_subsort_v<DispatchRequest, Tagged<DispatchRequest, tags::source::Sanitized>>);
static_assert(!s::is_payload_subsort_v<Tagged<int, tags::trust::Verified>, int>);
static_assert(!s::is_payload_subsort_v<Tagged<int, tags::access::RO>, int>);
static_assert(!s::is_payload_subsort_v<Tagged<int, tags::version::V<1>>, int>);
static_assert(!s::is_payload_subsort_v<Tagged<int, tags::source::Sanitized>, Tagged<int, tags::source::FromInternal>>);

static_assert(s::is_payload_subsort_v<Refined<::fixy::positive, Tagged<int, tags::source::Sanitized>>,
                                      Tagged<int, tags::source::Sanitized>>);
static_assert(s::is_payload_subsort_v<Refined<::fixy::positive, Tagged<int, tags::source::Sanitized>>, int>,
              "the order chains a drop of the refinement and a drop of the tag");
static_assert(!s::is_payload_subsort_v<Tagged<int, tags::source::Sanitized>,
                                       Refined<::fixy::positive, Tagged<int, tags::source::Sanitized>>>);

static_assert(s::is_payload_subsort_v<Refined<::fixy::positive, int>, Refined<::fixy::non_negative, int>>);
static_assert(!s::is_payload_subsort_v<Refined<::fixy::non_negative, int>, Refined<::fixy::positive, int>>);
static_assert(s::is_payload_subsort_v<Refined<::fixy::power_of_two, std::size_t>, Refined<::fixy::non_zero, std::size_t>>);
static_assert(!s::is_payload_subsort_v<Refined<::fixy::non_zero, int>, Refined<::fixy::non_negative, int>>);
static_assert(s::is_payload_subsort_v<Refined<::fixy::bounded_above<8u>, unsigned>, Refined<::fixy::bounded_above<16u>, unsigned>>);
static_assert(s::is_payload_subsort_v<Refined<::fixy::in_range<10, 20>, int>, Refined<::fixy::in_range<0, 100>, int>>);
static_assert(s::is_payload_subsort_v<Refined<::fixy::aligned<64>, void*>, Refined<::fixy::aligned<8>, void*>>);
static_assert(s::is_payload_subsort_v<Refined<::fixy::positive, int*>, Refined<::fixy::non_null, int*>>,
              "positive implies non_zero, and non_zero implies non_null: the order closes the chain that "
              "fixy/Refined.h does not close");

using BitexactTile = ::fixy::NumericalTier<Tolerance::BITEXACT, TensorTile>;
using Fp32Tile = ::fixy::NumericalTier<Tolerance::ULP_FP32, TensorTile>;
using Fp16Tile = ::fixy::NumericalTier<Tolerance::ULP_FP16, TensorTile>;
using RelaxedTile = ::fixy::NumericalTier<Tolerance::RELAXED, TensorTile>;
static_assert(s::is_payload_subsort_v<BitexactTile, RelaxedTile> && s::is_payload_subsort_v<Fp32Tile, Fp16Tile>);
static_assert(!s::is_payload_subsort_v<RelaxedTile, BitexactTile> && !s::is_payload_subsort_v<Fp16Tile, Fp32Tile>);
static_assert(!s::is_payload_subsort_v<BitexactTile, TensorTile>, "a tier does not drop to the bare value");

// Send lifts the order covariantly, Recv contravariantly.
static_assert(s::is_subtype_sync_v<Send<BitexactTile, End>, Send<RelaxedTile, End>>);
static_assert(!s::is_subtype_sync_v<Send<RelaxedTile, End>, Send<BitexactTile, End>>);
static_assert(s::is_subtype_sync_v<Recv<RelaxedTile, End>, Recv<BitexactTile, End>>);
static_assert(!s::is_subtype_sync_v<Recv<BitexactTile, End>, Recv<RelaxedTile, End>>);
static_assert(s::CompatibleClient<Send<BitexactTile, End>, Recv<RelaxedTile, End>>);
static_assert(!s::CompatibleClient<Send<RelaxedTile, End>, Recv<BitexactTile, End>>);
static_assert(s::CompatibleClient<Loop<Send<BitexactTile, Continue>>, Loop<Recv<RelaxedTile, Continue>>>);
static_assert(s::is_subtype_sync_v<Send<Refined<::fixy::positive, int>, End>, Send<int, End>>);
static_assert(!s::is_subtype_sync_v<Send<int, End>, Send<Refined<::fixy::positive, int>, End>>);
static_assert(s::is_subtype_sync_v<Recv<int, End>, Recv<Refined<::fixy::positive, int>, End>>);
static_assert(!s::is_subtype_sync_v<Recv<Refined<::fixy::positive, int>, End>, Recv<int, End>>);
static_assert(s::is_subtype_sync_v<Send<Tagged<int, tags::source::Sanitized>, End>, Send<int, End>>);
static_assert(!s::is_subtype_sync_v<Send<Tagged<int, tags::source::External>, End>, Send<int, End>>);
static_assert(s::is_subtype_sync_v<Select<Send<Refined<::fixy::positive, int>, End>,
                                          Send<Tagged<MemoryPlanByte, tags::source::Sanitized>, End>>,
                                   Select<Send<int, End>, Send<MemoryPlanByte, End>>>);
static_assert(s::is_subtype_sync_v<Offer<Recv<int, End>>, Offer<Recv<Refined<::fixy::positive, int>, End>>>);

// Each axiom keeps the representation, so a subtype payload has the
// size and the trivial copy of its supertype payload.
static_assert(sizeof(Refined<::fixy::positive, int>) == sizeof(int)
              && std::is_trivially_copyable_v<Refined<::fixy::positive, int>>);
static_assert(sizeof(Tagged<DispatchRequest, tags::source::Sanitized>) == sizeof(DispatchRequest)
              && std::is_trivially_copyable_v<Tagged<DispatchRequest, tags::source::Sanitized>>);
static_assert(sizeof(BitexactTile) == sizeof(RelaxedTile) && sizeof(BitexactTile) == sizeof(TensorTile)
              && std::is_trivially_copyable_v<BitexactTile>);

// ── Messages of a projection ─────────────────────────────────────────
//
// A projected local type names the peer and the label of each message
// in PeerMsg<Peer, Label, Payload>.  PeerMsg is covariant in its payload
// through the axiom peer_message of fixy/session/Projection.h, and exact
// in its peer and its label.

namespace projected {
struct Hello {};
struct Bye {};
using s::PeerMsg;
using HelloBob = PeerMsg<Bob, Hello, int>;
using ByeBob = PeerMsg<Bob, Bye, int>;
using Menu = Select<Send<HelloBob, End>, Send<ByeBob, End>>;
using Inbox = Offer<s::Sender<Bob>, Recv<PeerMsg<Bob, Hello, int>, End>, Recv<PeerMsg<Bob, Bye, int>, End>>;
}  // namespace projected

static_assert(s::is_subtype_sync_v<Send<projected::HelloBob, End>, Send<projected::HelloBob, End>>);
static_assert(s::is_subtype_sync_v<projected::Menu, projected::Menu>
              && s::is_subtype_sync_v<projected::Inbox, projected::Inbox>);
static_assert(s::subtype_mismatch_v<Send<projected::HelloBob, End>, Send<s::PeerMsg<Alice, projected::Hello, int>, End>>
                  == tr::mismatch::payload,
              "the peer is compared for identity");
static_assert(s::subtype_mismatch_v<Send<projected::HelloBob, End>, Send<projected::ByeBob, End>>
                  == tr::mismatch::payload,
              "the label is compared for identity");
static_assert(s::is_subtype_sync_v<Send<s::PeerMsg<Bob, projected::Hello, Refined<::fixy::positive, int>>, End>,
                                   Send<projected::HelloBob, End>>,
              "a payload inside a message follows the payload order, through the congruence axiom peer_message");
static_assert(!s::is_subtype_sync_v<Send<s::PeerMsg<Bob, projected::Hello, Tagged<int, tags::source::FromUser>>, End>,
                                    Send<projected::HelloBob, End>>,
              "an untrusted payload inside a message does not drop its tag");
static_assert(s::is_subtype_sync_v<Select<Send<projected::HelloBob, End>>, projected::Menu>,
              "an output choice of the subtype can drop the last label");
static_assert(!s::is_subtype_sync_v<Select<Send<projected::ByeBob, End>>, projected::Menu>,
              "branches match by position, so a choice that keeps only the second label is refused");
static_assert(s::is_subtype_sync_v<projected::Inbox, Offer<s::Sender<Bob>, Recv<projected::HelloBob, End>>>,
              "an input choice of the subtype can add a label at the end");
static_assert(s::subtype_mismatch_v<projected::Inbox,
                                    Offer<s::Sender<Alice>, Recv<projected::HelloBob, End>,
                                          Recv<projected::ByeBob, End>>>
                  == tr::mismatch::annotation,
              "the sender of an input choice is compared for identity");

// ── The asynchronous relation ────────────────────────────────────────

using Early = Send<PingReq, Recv<StopReq, End>>;
using Late = Recv<StopReq, Send<PingReq, End>>;
static_assert(!s::is_subtype_sync_v<Early, Late>, "the synchronous relation keeps the order");
static_assert(s::is_subtype_async_v<Early, Late, 1>, "an output moves ahead of an input");
static_assert(!s::is_subtype_async_v<Early, Late, 0>, "no buffer, no anticipation");
static_assert(!s::is_subtype_async_v<Late, Early, 8>, "an input never moves ahead of an output");

using Early2 = Send<PingReq, Send<PingReq, Recv<StopReq, Recv<StopReq, End>>>>;
using Late2 = Recv<StopReq, Recv<StopReq, Send<PingReq, Send<PingReq, End>>>>;
static_assert(!s::is_subtype_async_v<Early2, Late2, 1>, "two messages ahead need a buffer of two");
static_assert(s::is_subtype_async_v<Early2, Late2, 2>);

// An orphan: the subtype sends a message the supertype never sends.
static_assert(!s::is_subtype_async_v<Send<PingReq, End>, End, 4>);
static_assert(!s::is_subtype_async_v<End, Send<PingReq, End>, 4>);

// A loop that anticipates one message for ever needs an unbounded
// buffer, so no capacity proves it.
using Flood = Loop<Send<PingReq, Continue>>;
using Paced = Loop<Recv<StopReq, Send<PingReq, Continue>>>;
static_assert(!s::is_subtype_async_v<Flood, Paced, 4>);

// A loop that sends one message ahead and then keeps the pace.
using Ahead = Send<PingReq, Loop<Recv<StopReq, Send<PingReq, Continue>>>>;
using Beat = Loop<Recv<StopReq, Send<PingReq, Continue>>>;
static_assert(!s::is_subtype_sync_v<Ahead, Beat>);

// The synchronous relation is a subset, at every capacity.
static_assert(s::is_subtype_async_v<DS1, DS2, 0> && s::is_subtype_async_v<DO1, DO2, 3>);
static_assert(s::is_subtype_async_v<Loop<Send<int, Continue>>, Send<int, Loop<Send<int, Continue>>>, 0>);

// Closure under duality holds by construction.
static_assert(s::is_subtype_async_v<s::dual_of_t<Late>, s::dual_of_t<Early>, 1>);
static_assert(s::is_subtype_async_v<Early2, Late2, 2> == s::is_subtype_async_v<s::dual_of_t<Late2>, s::dual_of_t<Early2>, 2>);

// A choice moves as one message.
using EarlyPick = Select<Send<PingReq, Recv<StopReq, End>>, Send<Job, Recv<StopReq, End>>>;
using LatePick = Recv<StopReq, Select<Send<PingReq, End>, Send<Job, End>>>;
static_assert(s::is_subtype_async_v<Select<Recv<StopReq, End>>, Recv<StopReq, Select<End>>, 1>);
static_assert(!s::is_subtype_async_v<EarlyPick, LatePick, 1>, "the label moves ahead, but the payloads differ in order");

static_assert(foundation::contracts::armed_cell_holds_v<s::is_sync_subtype>);
static_assert(foundation::contracts::armed_cell_holds_v<s::is_async_subtype>);

// ── Generated protocols ──────────────────────────────────────────────
//
// A linear congruential generator with a fixed seed builds protocols
// over a small payload alphabet.  `widen` builds a supertype by rules
// the relation must admit: a Send payload rises in the payload order, a
// Recv payload falls, a Select gains a branch, an Offer loses its last
// branch.  Each chain p0 ⩽ p1 ⩽ p2 then checks reflexivity,
// transitivity, closure under duality, the involution of duality, the
// well-formedness of each dual, and that the asynchronous relation
// holds each synchronous pair.

struct lcg {
    std::uint64_t state = 0x9e3779b97f4a7c15ULL;
    consteval std::uint64_t next() {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return state >> 33;
    }
    consteval std::size_t below(std::size_t bound) { return next() % bound; }
};

// Payloads in the order the chains move along: a Send payload moves one
// step up, a Recv payload one step down.
consteval std::vector<std::meta::info> payload_ladder() {
    return {^^Refined<::fixy::positive, int>, ^^Refined<::fixy::non_negative, int>, ^^int};
}

consteval std::size_t ladder_index(std::meta::info payload) {
    const std::vector<std::meta::info> ladder = payload_ladder();
    for (std::size_t index = 0; index < ladder.size(); ++index) {
        if (ladder[index] == payload) return index;
    }
    return ladder.size();
}

constexpr std::meta::info send_shape = ^^s::Send;
constexpr std::meta::info recv_shape = ^^s::Recv;
constexpr std::meta::info select_shape = ^^s::Select;
constexpr std::meta::info offer_shape = ^^s::Offer;
constexpr std::meta::info loop_shape = ^^s::Loop;

consteval std::meta::info generate(lcg& random, std::size_t depth, std::size_t loops, bool is_guarded) {
    const std::vector<std::meta::info> ladder = payload_ladder();
    if (depth == 0) {
        if (loops > 0 && is_guarded && random.below(2) == 0) return ^^s::Continue;
        return ^^s::End;
    }
    switch (random.below(6)) {
        case 0:
            return std::meta::substitute(send_shape, {ladder[random.below(ladder.size())],
                                                      generate(random, depth - 1, loops, true)});
        case 1:
            return std::meta::substitute(recv_shape, {ladder[random.below(ladder.size())],
                                                      generate(random, depth - 1, loops, true)});
        case 2: {
            std::vector<std::meta::info> branches;
            const std::size_t count = 1 + random.below(2);
            for (std::size_t index = 0; index < count; ++index) branches.push_back(generate(random, depth - 1, loops, true));
            return std::meta::substitute(select_shape, branches);
        }
        case 3: {
            std::vector<std::meta::info> branches;
            const std::size_t count = 1 + random.below(2);
            for (std::size_t index = 0; index < count; ++index) branches.push_back(generate(random, depth - 1, loops, true));
            return std::meta::substitute(offer_shape, branches);
        }
        case 4:
            return std::meta::substitute(
                loop_shape, {std::meta::substitute(send_shape, {^^int, generate(random, depth - 1, loops + 1, true)})});
        default:
            return generate(random, 0, loops, is_guarded);
    }
}

// A supertype: every rule here is one the relation admits.
consteval std::meta::info widen(std::meta::info type) {
    const std::meta::info plain = std::meta::dealias(type);
    if (!std::meta::has_template_arguments(plain)) return plain;
    const std::meta::info shape = std::meta::template_of(plain);
    std::vector<std::meta::info> arguments = std::meta::template_arguments_of(plain);
    const std::vector<std::meta::info> ladder = payload_ladder();
    if (shape == send_shape) {
        const std::size_t index = ladder_index(arguments[0]);
        if (index + 1 < ladder.size()) arguments[0] = ladder[index + 1];
        arguments[1] = widen(arguments[1]);
        return std::meta::substitute(send_shape, arguments);
    }
    if (shape == recv_shape) {
        const std::size_t index = ladder_index(arguments[0]);
        if (index > 0 && index < ladder.size()) arguments[0] = ladder[index - 1];
        arguments[1] = widen(arguments[1]);
        return std::meta::substitute(recv_shape, arguments);
    }
    if (shape == select_shape) {
        for (std::meta::info& branch : arguments) branch = widen(branch);
        arguments.push_back(^^s::End);
        return std::meta::substitute(select_shape, arguments);
    }
    if (shape == offer_shape) {
        if (arguments.size() > 1) arguments.pop_back();
        for (std::meta::info& branch : arguments) branch = widen(branch);
        return std::meta::substitute(offer_shape, arguments);
    }
    if (shape == loop_shape) return std::meta::substitute(loop_shape, {widen(arguments[0])});
    return plain;
}

consteval bool sync(std::meta::info sub, std::meta::info super) {
    return std::meta::extract<bool>(std::meta::substitute(^^s::is_subtype_sync_v, {sub, super}));
}
consteval bool async_at_one(std::meta::info sub, std::meta::info super) {
    return std::meta::extract<bool>(
        std::meta::substitute(^^s::is_subtype_async_v, {sub, super, std::meta::reflect_constant(std::size_t{1})}));
}
consteval std::meta::info dual(std::meta::info type) {
    return std::meta::dealias(std::meta::substitute(^^s::dual_of_t, {type}));
}
consteval bool well_formed(std::meta::info type) {
    return std::meta::extract<bool>(std::meta::substitute(^^s::is_well_formed_v, {type}));
}

struct law_counts {
    std::size_t chains = 0;
    std::size_t reflexive = 0;
    std::size_t widened = 0;
    std::size_t transitive = 0;
    std::size_t dual_closed = 0;
    std::size_t involutive = 0;
    std::size_t dual_well_formed = 0;
    std::size_t async_contains_sync = 0;
    std::size_t pair_closure = 0;
    std::size_t pairs = 0;
};

inline constexpr std::size_t generated_chains = 16;

consteval law_counts check_generated_laws() {
    law_counts counts{};
    lcg random{};
    std::vector<std::meta::info> seen;
    for (std::size_t chain = 0; chain < generated_chains; ++chain) {
        const std::meta::info p0 = generate(random, 4, 0, true);
        const std::meta::info p1 = widen(p0);
        const std::meta::info p2 = widen(p1);
        ++counts.chains;
        if (sync(p0, p0) && sync(p1, p1) && sync(p2, p2)) ++counts.reflexive;
        if (sync(p0, p1) && sync(p1, p2)) ++counts.widened;
        if (!sync(p0, p1) || !sync(p1, p2) || sync(p0, p2)) ++counts.transitive;
        if (sync(dual(p1), dual(p0)) && sync(dual(p2), dual(p1)) && sync(dual(p2), dual(p0))) ++counts.dual_closed;
        if (dual(dual(p0)) == p0 && dual(dual(p2)) == p2) ++counts.involutive;
        if (well_formed(p0) && well_formed(dual(p0)) && well_formed(dual(p2))) ++counts.dual_well_formed;
        if (async_at_one(p0, p1) && async_at_one(p1, p2) && async_at_one(p0, p0)) ++counts.async_contains_sync;
        seen.push_back(p0);
        seen.push_back(p2);
    }
    for (const std::meta::info left : seen) {
        for (const std::meta::info right : seen) {
            ++counts.pairs;
            if (sync(left, right) == sync(dual(right), dual(left))) ++counts.pair_closure;
        }
    }
    return counts;
}

inline constexpr law_counts generated = check_generated_laws();

static_assert(generated.chains == generated_chains);
static_assert(generated.reflexive == generated_chains, "the relation is reflexive on each generated protocol");
static_assert(generated.widened == generated_chains, "each widening rule is admitted by the relation");
static_assert(generated.transitive == generated_chains, "the relation is transitive on each generated chain");
static_assert(generated.dual_closed == generated_chains, "the relation is closed under duality on each chain");
static_assert(generated.involutive == generated_chains, "duality is an involution without notes");
static_assert(generated.dual_well_formed == generated_chains, "the dual of a well-formed protocol is well-formed");
static_assert(generated.async_contains_sync == generated_chains, "the asynchronous relation holds each synchronous pair");
static_assert(generated.pair_closure == generated.pairs,
              "on every generated pair, T refines U exactly when the dual of U refines the dual of T");

}  // namespace

int main() {
    // The generated counts reach the program, so a law that no longer
    // holds shows here as well as in the build.
    const bool holds = generated.reflexive == generated.chains && generated.transitive == generated.chains
                       && generated.dual_closed == generated.chains && generated.pair_closure == generated.pairs;
    if (!holds) {
        std::fprintf(stderr, "test_session_subtype: a generated law does not hold\n");
        return 1;
    }
    return 0;
}
