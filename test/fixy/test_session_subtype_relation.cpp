// The synchronous relation of fixy/session/Subtype.h on each combinator:
// reflexivity, the shapes that do not relate, width, position, recursion,
// the branches that are no label and the labels that a payload names.

#include "session_subtype.h"

namespace test_session_subtype_types {

// ── Reflexivity on every combinator ──────────────────────────────────

static_assert(s::is_subtype_sync_v<End, End>);
static_assert(s::is_subtype_sync_v<Send<int, End>, Send<int, End>>);
static_assert(s::is_subtype_sync_v<Recv<int, End>, Recv<int, End>>);
static_assert(s::is_subtype_sync_v<Loop<Send<int, Continue>>, Loop<Send<int, Continue>>>);
static_assert(s::is_subtype_sync_v<Select<Send<int, End>, Recv<bool, End>>, Select<Send<int, End>, Recv<bool, End>>>);
static_assert(s::is_subtype_sync_v<Offer<Recv<int, End>, Send<bool, End>>, Offer<Recv<int, End>, Send<bool, End>>>);
static_assert(s::is_subtype_sync_v<Offer<Sender<Alice>, Recv<int, End>>, Offer<Sender<Alice>, Recv<int, End>>>,
              "the relation must be reflexive on an Offer with a note");
static_assert(!s::is_subtype_sync_v<Offer<Sender<Alice>>, Offer<Sender<Alice>>>,
              "an Offer with a note and no branch is an empty choice, which is not well-formed");
static_assert(s::is_subtype_sync_v<Loop<Loop<Send<int, Continue>>>, Loop<Loop<Send<int, Continue>>>>);

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
// included.
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
static_assert(!s::is_subtype_async_v<Select<>, Select<Send<PingReq, End>>, Slots<4>>);
static_assert(!s::is_subtype_sync_v<Send<int, Select<>>, Send<int, Select<Send<PingReq, End>>>>,
              "an empty choice below the top is refused too");
static_assert(!s::is_subtype_sync_v<Select<Send<PingReq, End>, Send<StopReq, End>>, Select<Send<PingReq, End>>>);
static_assert(s::is_subtype_sync_v<Offer<Recv<PingReq, End>, Recv<StopReq, End>, End>,
                                   Offer<Recv<PingReq, End>, Recv<StopReq, End>>>);
static_assert(!s::is_subtype_sync_v<Offer<Recv<PingReq, End>>, Offer<Recv<PingReq, End>, Recv<StopReq, End>>>);
static_assert(!s::is_subtype_sync_v<Offer<>, Offer<Recv<PingReq, End>>>);
static_assert(!s::is_subtype_sync_v<Offer<>, Offer<>>);
static_assert(s::subtype_mismatch_v<Offer<>, Offer<>> == tr::mismatch::ill_formed);
// A narrower Select that keeps an exit refines, and one that drops the
// only exit of the loop does not: it can never end where the supertype
// can (fair subtyping).
static_assert(s::is_subtype_sync_v<Loop<Select<Send<PingReq, Continue>, Send<StopReq, End>>>,
                                   Loop<Select<Send<PingReq, Continue>, Send<StopReq, End>, Send<int, End>>>>);
static_assert(s::subtype_mismatch_v<Loop<Select<Send<PingReq, Continue>>>,
                                    Loop<Select<Send<PingReq, Continue>, Send<StopReq, End>>>>
              == tr::mismatch::loses_termination);
// A stream refines a stream, because the supertype never ends either.
static_assert(s::is_subtype_sync_v<Loop<Select<Send<PingReq, Continue>>>,
                                   Loop<Select<Send<PingReq, Continue>, Send<StopReq, Continue>>>>);
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
// A PeerMsg names a peer and a label, and its label key is the label
// alone.  Two branches of one choice that name the same label are not
// well-formed, whatever their payloads and their peers.

static_assert(!s::is_well_formed_v<
              Select<Send<s::PeerMsg<Bob, PingReq, int>, End>, Send<s::PeerMsg<Bob, PingReq, int>, Send<int, End>>>>);
static_assert(
    !s::is_well_formed_v<Select<Send<s::PeerMsg<Bob, PingReq, int>, End>, Send<s::PeerMsg<Bob, PingReq, bool>, End>>>,
    "the payload is not part of the label");
static_assert(
    !s::is_well_formed_v<Select<Send<s::PeerMsg<Bob, PingReq, int>, End>, Send<s::PeerMsg<Alice, PingReq, int>, End>>>,
    "the peer is not part of the label, so each side of a channel puts one word on the wire");
static_assert(!s::is_well_formed_v<
              Offer<Sender<Bob>, Recv<s::PeerMsg<Bob, PingReq, int>, End>, Recv<s::PeerMsg<Bob, PingReq, int>, End>>>);

// The relation is on the unfoldings, so a loop and its unfolding relate
// in both directions.  A lockstep walk of the syntax cannot see that.
static_assert(s::is_subtype_sync_v<Loop<Send<int, Continue>>, Send<int, Loop<Send<int, Continue>>>>);
static_assert(s::is_subtype_sync_v<Send<int, Loop<Send<int, Continue>>>, Loop<Send<int, Continue>>>);
static_assert(s::equivalent_sync_v<Loop<Send<int, Send<int, Continue>>>, Loop<Send<int, Continue>>>);
static_assert(!s::is_subtype_sync_v<Loop<Send<int, Continue>>, Send<int, End>>);
static_assert(!s::is_subtype_sync_v<Send<int, End>, Loop<Send<int, Continue>>>);

namespace evolution {
using ServerV1 =
    Loop<Offer<Recv<Req, Send<Resp, Continue>>, Recv<CloseCmd, End>, Recv<PingReq, Send<PingReq, Continue>>>>;
using ServerV2 = Loop<Offer<Recv<Req, Send<Resp, Continue>>, Recv<CloseCmd, End>,
                            Recv<PingReq, Send<PingReq, Continue>>, Recv<StopReq, Send<Resp, End>>>>;
static_assert(s::is_subtype_sync_v<ServerV2, ServerV1> && !s::is_subtype_sync_v<ServerV1, ServerV2>);

consteval bool evolution_holds() {
    s::check_protocol_evolution<ServerV1, ServerV2>();
    return true;
}
static_assert(evolution_holds());
}  // namespace evolution

namespace mpmc {
using ProducerFull = Loop<Select<Send<Job, Continue>, Send<Job, Continue>, End>>;
using ProducerNarrow = Loop<Select<Send<Job, Continue>, Send<Job, Continue>>>;
static_assert(!s::is_subtype_sync_v<ProducerFull, ProducerNarrow>);
// A producer that never stops does not refine one that can stop: the
// consumer waits for the stop and never gets it.
static_assert(s::subtype_mismatch_v<ProducerNarrow, ProducerFull> == tr::mismatch::loses_termination);
}  // namespace mpmc

}  // namespace test_session_subtype_types
