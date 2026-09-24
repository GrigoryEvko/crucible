// A role's projected local protocol can hold several Offer nodes whose
// senders are different remote roles.  Crash analysis asks of each one
// whether it needs a branch for a given peer's crash, and the answer is
// yes only when that peer is the Offer's declared sender.  Everything
// below turns on that single asymmetry.

#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionCrash.h>

#include <cstdio>
#include <tuple>
#include <type_traits>

namespace {

using namespace crucible::safety::proto;

struct Alice {};
struct Bob {};
struct Carol {};

struct Msg {};
struct Ack {};

static_assert(std::is_same_v<offer_sender_t<Offer<Recv<Msg, End>, Recv<Ack, End>>>, AnonymousPeer>,
              "Unannotated Offer<> should resolve to AnonymousPeer sender");

static_assert(std::is_same_v<offer_sender_t<Offer<Sender<Alice>, Recv<Msg, End>, Recv<Ack, End>>>, Alice>,
              "Annotated Offer<Sender<Alice>, ...> should resolve to Alice");

using UnannotatedOffer = Offer<Recv<Msg, End>, Recv<Ack, End>>;
using AnnotatedOffer = Offer<Sender<Alice>, Recv<Msg, End>, Recv<Ack, End>>;

static_assert(UnannotatedOffer::branch_count == 2);
static_assert(AnnotatedOffer::branch_count == 2, "Sender<Role> tag must not be counted as a branch");

static_assert(std::is_same_v<AnnotatedOffer::branches_tuple, std::tuple<Recv<Msg, End>, Recv<Ack, End>>>,
              "branches_tuple excludes the Sender<Role> tag");

using AliceOfferWithCrash = Offer<Sender<Alice>, Recv<Msg, End>, Recv<Crash<Alice>, End>>;
static_assert(has_crash_branch_for_peer_v<AliceOfferWithCrash, Alice>);

using AliceOfferNoCrash = Offer<Sender<Alice>, Recv<Msg, End>, Recv<Ack, End>>;
static_assert(!has_crash_branch_for_peer_v<AliceOfferNoCrash, Alice>);

static_assert(has_crash_branch_for_peer_v<AliceOfferNoCrash, Bob>,
              "an Offer from Alice is not Bob's Offer, so it is vacuously "
              "crash-safe for Bob");

// An Offer from Bob carries no obligation toward Alice or Carol either.
using BobOfferNoCrash = Offer<Sender<Bob>, Recv<Msg, End>, Recv<Ack, End>>;
static_assert(has_crash_branch_for_peer_v<BobOfferNoCrash, Alice>);
static_assert(has_crash_branch_for_peer_v<BobOfferNoCrash, Carol>);

using MpstCrashSafe =
    Offer<Sender<Alice>, Recv<Msg, Offer<Sender<Bob>, Recv<Ack, End>, Recv<Crash<Bob>, End>>>, Recv<Crash<Alice>, End>>;

static_assert(every_offer_has_crash_branch_for_peer_v<MpstCrashSafe, Alice>);
static_assert(every_offer_has_crash_branch_for_peer_v<MpstCrashSafe, Bob>);
static_assert(every_offer_has_crash_branch_for_peer_v<MpstCrashSafe, Carol>,
              "neither Offer is from Carol, so the whole-tree predicate is vacuous");

// The Alice check fails once Alice's own Offer drops her crash branch.
using MpstAliceMissing =
    Offer<Sender<Alice>, Recv<Msg, Offer<Sender<Bob>, Recv<Ack, End>, Recv<Crash<Bob>, End>>>, Recv<Ack, End>>;
static_assert(!every_offer_has_crash_branch_for_peer_v<MpstAliceMissing, Alice>);
// The Bob check still passes on the same tree, because Bob's Offer is
// intact.
static_assert(every_offer_has_crash_branch_for_peer_v<MpstAliceMissing, Bob>);

using ComposedAnn = compose_t<Offer<Sender<Alice>, Send<Msg, End>, Send<Ack, End>>, Recv<Ack, End>>;
using ExpectedComposedAnn = Offer<Sender<Alice>, Send<Msg, Recv<Ack, End>>, Send<Ack, Recv<Ack, End>>>;
static_assert(std::is_same_v<ComposedAnn, ExpectedComposedAnn>,
              "compose into annotated Offer must preserve Sender<Role>");

// The index counts real branches and skips the sender tag, so 1 selects
// the second Send.
using RewrittenAnn = compose_at_branch_t<Offer<Sender<Bob>, Send<Msg, End>, Send<Ack, End>>,
                                         /*I=*/1, Recv<Msg, End>>;
using ExpectedRewrittenAnn = Offer<Sender<Bob>, Send<Msg, End>, Send<Ack, Recv<Msg, End>>>;
static_assert(std::is_same_v<RewrittenAnn, ExpectedRewrittenAnn>,
              "compose_at_branch on annotated Offer indexes over real branches and preserves Sender<Role>");

// Taking the dual drops the sender tag, which breaks involution: the dual
// of the dual is no longer the original protocol.  A trait reports that,
// so generic rewriting code can refuse such a shape rather than silently
// losing the annotation.
using AnnDual = dual_of_t<Offer<Sender<Alice>, Recv<Msg, End>, Recv<Ack, End>>>;
using ExpectedAnnDual = Select<Send<Msg, End>, Send<Ack, End>>;
static_assert(std::is_same_v<AnnDual, ExpectedAnnDual>,
              "taking the dual of a sender-annotated Offer drops the tag; the "
              "role-dependent dual belongs to the projection machinery");
static_assert(!is_dual_involutive_v<Offer<Sender<Alice>, Recv<Msg, End>, Recv<Ack, End>>>,
              "a sender-annotated Offer is not dual-involutive, and the trait must "
              "surface that asymmetry to generic code");
static_assert(is_dual_involutive_v<Offer<Recv<Msg, End>, Recv<Ack, End>>>, "an unannotated Offer is dual-involutive");
static_assert(is_dual_involutive_v<Send<Msg, End>>, "a closed-core protocol is dual-involutive");
static_assert(is_dual_involutive_v<Loop<Select<Send<Msg, Continue>, End>>>,
              "the trait recurses through Loop and Select");

static_assert(is_empty_choice_v<Offer<>>, "an unannotated Offer with no branches is empty");
static_assert(is_empty_choice_v<Offer<Sender<Alice>>>,
              "an annotated Offer with no branches is empty too, and no runnable "
              "handle may be constructible on it");

static_assert(is_well_formed_v<Offer<Sender<Alice>, Recv<Msg, End>, Recv<Ack, End>>>,
              "the sender tag does not disturb well-formedness");

}  // namespace

int main() {
    std::printf("test_session_sender_role: PASSED (compile-time witnesses).\n");
    return 0;
}
