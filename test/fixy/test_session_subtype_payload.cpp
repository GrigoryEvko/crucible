// The payload order of fixy/session/Subtype.h, its lift through Send and
// Recv, and the messages of a projection, which name a peer and a label
// in PeerMsg<Peer, Label, Payload>.

#include "session_subtype.h"

#include <cstddef>
#include <type_traits>

namespace test_session_subtype_types {

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
static_assert(
    s::is_payload_subsort_v<Refined<::fixy::power_of_two, std::size_t>, Refined<::fixy::non_zero, std::size_t>>);
static_assert(!s::is_payload_subsort_v<Refined<::fixy::non_zero, int>, Refined<::fixy::non_negative, int>>);
static_assert(s::is_payload_subsort_v<Refined<::fixy::bounded_above<8u>, unsigned>,
                                      Refined<::fixy::bounded_above<16u>, unsigned>>);
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
static_assert(!s::is_payload_subsort_v<TensorTile, BitexactTile>, "a bare value does not gain a tier");

// Send lifts the order covariantly, Recv contravariantly.
static_assert(s::is_subtype_sync_v<Send<BitexactTile, End>, Send<RelaxedTile, End>>);
static_assert(!s::is_subtype_sync_v<Send<RelaxedTile, End>, Send<BitexactTile, End>>);
static_assert(s::is_subtype_sync_v<Recv<RelaxedTile, End>, Recv<BitexactTile, End>>);
static_assert(!s::is_subtype_sync_v<Recv<BitexactTile, End>, Recv<RelaxedTile, End>>);
static_assert(s::CompatibleClient<Send<BitexactTile, End>, Recv<RelaxedTile, End>>);
static_assert(!s::CompatibleClient<Send<RelaxedTile, End>, Recv<BitexactTile, End>>);
static_assert(s::CompatibleClient<Loop<Send<BitexactTile, Continue>>, Loop<Recv<RelaxedTile, Continue>>>);
static_assert(!s::is_subtype_sync_v<Loop<Send<BitexactTile, Send<RelaxedTile, Continue>>>,
                                    Loop<Send<BitexactTile, Send<BitexactTile, Continue>>>>,
              "one step of a loop body that sends a weaker tier makes the whole loop weaker");
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
// size of its supertype payload and a trivial copy construction.
static_assert(sizeof(Refined<::fixy::positive, int>) == sizeof(int)
              && std::is_trivially_copy_constructible_v<Refined<::fixy::positive, int>>);
static_assert(sizeof(Tagged<DispatchRequest, tags::source::Sanitized>) == sizeof(DispatchRequest)
              && std::is_trivially_copy_constructible_v<Tagged<DispatchRequest, tags::source::Sanitized>>);
static_assert(sizeof(BitexactTile) == sizeof(RelaxedTile) && sizeof(BitexactTile) == sizeof(TensorTile)
              && std::is_trivially_copyable_v<BitexactTile>);

// ── Messages of a projection ─────────────────────────────────────────
//
// A projected local type names the peer and the label of each message
// in PeerMsg<Peer, Label, Payload>.  PeerMsg is covariant in its payload
// through the axiom peer_message of fixy/session/Subtype.h, and exact
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
              "the label pairs the two messages, and the payload order keeps the peer exact");
static_assert(s::subtype_mismatch_v<Send<projected::HelloBob, End>, Send<projected::ByeBob, End>>
                  == tr::mismatch::label_set,
              "the label is compared for identity");
static_assert(s::is_subtype_sync_v<Send<s::PeerMsg<Bob, projected::Hello, Refined<::fixy::positive, int>>, End>,
                                   Send<projected::HelloBob, End>>,
              "a payload inside a message follows the payload order, through the congruence axiom peer_message");
static_assert(!s::is_subtype_sync_v<Send<s::PeerMsg<Bob, projected::Hello, Tagged<int, tags::source::FromUser>>, End>,
                                    Send<projected::HelloBob, End>>,
              "an untrusted payload inside a message does not drop its tag");
static_assert(s::is_subtype_sync_v<Select<Send<projected::HelloBob, End>>, projected::Menu>,
              "an output choice of the subtype can drop a label");
static_assert(s::is_subtype_sync_v<Select<Send<projected::ByeBob, End>>, projected::Menu>,
              "a keyed choice pairs by label, so a choice that keeps only the second label refines");
static_assert(
    s::is_subtype_sync_v<Select<Send<projected::ByeBob, End>, Send<projected::HelloBob, End>>, projected::Menu>,
    "a keyed choice with its labels in another order is the same choice");
static_assert(s::is_subtype_sync_v<projected::Inbox, Offer<s::Sender<Bob>, Recv<projected::HelloBob, End>>>,
              "an input choice of the subtype can add a label");
static_assert(s::is_subtype_sync_v<projected::Inbox, Offer<s::Sender<Bob>, Recv<projected::ByeBob, End>>>,
              "an input choice of the subtype can add a label before the labels of the supertype");
static_assert(s::subtype_mismatch_v<Offer<s::Sender<Bob>, Recv<projected::ByeBob, End>>, projected::Inbox>
                  == tr::mismatch::label_set,
              "an input choice of the subtype receives each label of the supertype");
static_assert(
    s::subtype_mismatch_v<projected::Inbox,
                          Offer<s::Sender<Alice>, Recv<projected::HelloBob, End>, Recv<projected::ByeBob, End>>>
        == tr::mismatch::annotation,
    "the sender of an input choice is compared for identity");

// A keyed step is a choice with one branch: p⊕q:m(B) and p&q:m(B) of
// Definition 4.9 of the crash-stop paper.  So a keyed Send pairs with a
// Select by label, and a keyed Recv with an Offer, and each reads as the
// choice of its one branch, the Offer with the note of its sender.
namespace projected {
struct Retry {};
using RetryBob = PeerMsg<Bob, Retry, int>;
}  // namespace projected
static_assert(s::is_subtype_sync_v<projected::Inbox, Recv<projected::HelloBob, End>>,
              "an Offer that receives more labels stands for a keyed Recv");
static_assert(s::is_subtype_sync_v<Send<projected::HelloBob, End>, projected::Menu>,
              "a keyed Send sends one label of the Select");
static_assert(s::subtype_mismatch_v<Recv<projected::RetryBob, End>, projected::Inbox> == tr::mismatch::label_set
                  && s::subtype_mismatch_v<projected::Inbox, Recv<projected::RetryBob, End>> == tr::mismatch::label_set,
              "a keyed Recv of a label the Offer does not name is refused both ways");
static_assert(s::subtype_mismatch_v<Send<projected::RetryBob, End>, projected::Menu> == tr::mismatch::label_set
                  && s::subtype_mismatch_v<projected::Menu, Send<projected::RetryBob, End>> == tr::mismatch::label_set,
              "a keyed Send of a label the Select does not name is refused both ways");
static_assert(
    s::equivalent_sync_v<Send<projected::HelloBob, End>, Select<Send<projected::HelloBob, End>>>
        && s::equivalent_sync_v<Recv<projected::HelloBob, End>, Offer<s::Sender<Bob>, Recv<projected::HelloBob, End>>>,
    "a keyed step and the choice of its one branch are one type");
static_assert(s::subtype_mismatch_v<Offer<Recv<projected::HelloBob, End>>, Recv<projected::HelloBob, End>>
                  == tr::mismatch::annotation,
              "a keyed Recv of a message from Bob is the Offer that Bob signals");
static_assert(s::is_subtype_sync_v<Send<int, End>, Send<int, End>>
                  && s::subtype_mismatch_v<Send<int, End>, Select<Send<int, End>>> == tr::mismatch::shape,
              "a step whose payload names no label stays a plain step");

}  // namespace test_session_subtype_types
