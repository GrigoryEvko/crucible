// Compile-time witnesses for Sender<Role> annotation on Offer<> (#367).
//
// In MPST (Honda-Yoshida-Carbone 2008), a role's projected local
// protocol can contain multiple Offer<> nodes whose senders are
// different remote roles.  Crash analysis must ask "does THIS Offer
// need a Recv<Crash<PeerTag>, _> branch?" — the answer is YES only
// when PeerTag is the Offer's declared sender.
//
// This file pins down:
//   * Offer<Sender<Role>, ...> carries sender = Role (offer_sender_t).
//   * Offer<Branches...> without the tag carries sender = AnonymousPeer.
//   * branch_count / branches_tuple exclude the Sender<> tag for the
//     annotated form (the real branches only).
//   * has_crash_branch_for_peer_v is vacuously true when sender ≠
//     queried PeerTag ("not your peer, not your problem").
//   * every_offer_has_crash_branch_for_peer_v walks the tree and
//     skips Offers from peers other than PeerTag — an MPST
//     protocol with Offers from Alice AND Bob, where the Alice
//     Offers all handle Alice's crash, passes the Alice check even
//     if the Bob Offers don't handle Alice's crash.
//   * compose / compose_at_branch preserve the Sender<Role> tag
//     through type-level rewrites.
//
// Runtime main() just prints PASSED — all claims are static_asserts.

#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionCrash.h>

#include <cstdio>
#include <tuple>
#include <type_traits>

namespace {

using namespace crucible::safety::proto;

// Role tags — phantom types (no storage, no members).
struct Alice {};
struct Bob   {};
struct Carol {};

// Message payloads.
struct Msg {};
struct Ack {};

// ── Baseline: sender extraction ──────────────────────────────────

static_assert(std::is_same_v<
    offer_sender_t<Offer<Recv<Msg, End>, Recv<Ack, End>>>,
    AnonymousPeer>,
    "Unannotated Offer<> should resolve to AnonymousPeer sender");

static_assert(std::is_same_v<
    offer_sender_t<Offer<Sender<Alice>, Recv<Msg, End>, Recv<Ack, End>>>,
    Alice>,
    "Annotated Offer<Sender<Alice>, ...> should resolve to Alice");

// ── branch_count / branches_tuple transparency ───────────────────

using UnannotatedOffer = Offer<Recv<Msg, End>, Recv<Ack, End>>;
using AnnotatedOffer   = Offer<Sender<Alice>, Recv<Msg, End>, Recv<Ack, End>>;

static_assert(UnannotatedOffer::branch_count == 2);
static_assert(AnnotatedOffer::branch_count   == 2,
    "Sender<Role> tag must not be counted as a branch");

static_assert(std::is_same_v<
    AnnotatedOffer::branches_tuple,
    std::tuple<Recv<Msg, End>, Recv<Ack, End>>>,
    "branches_tuple excludes the Sender<Role> tag");

// ── has_crash_branch_for_peer: annotated Offer, sender matches ──

// Annotated Offer FROM Alice WITH Crash<Alice> branch — OK.
using AliceOfferWithCrash = Offer<Sender<Alice>,
    Recv<Msg, End>,
    Recv<Crash<Alice>, End>>;
static_assert(has_crash_branch_for_peer_v<AliceOfferWithCrash, Alice>);

// Annotated Offer FROM Alice WITHOUT Crash<Alice> branch — FAIL.
using AliceOfferNoCrash = Offer<Sender<Alice>,
    Recv<Msg, End>,
    Recv<Ack, End>>;
static_assert(!has_crash_branch_for_peer_v<AliceOfferNoCrash, Alice>);

// ── has_crash_branch_for_peer: annotated Offer, sender mismatch ──

// Annotated Offer FROM Alice — no obligation toward Bob's crash
// branch.  Vacuously true.
static_assert(has_crash_branch_for_peer_v<AliceOfferNoCrash, Bob>,
    "Offer<Sender<Alice>, ...> is not Bob's Offer — vacuously crash-safe for Bob");

// Same: Offer from Bob is not Alice's Offer — vacuous.
using BobOfferNoCrash = Offer<Sender<Bob>,
    Recv<Msg, End>,
    Recv<Ack, End>>;
static_assert(has_crash_branch_for_peer_v<BobOfferNoCrash, Alice>);
static_assert(has_crash_branch_for_peer_v<BobOfferNoCrash, Carol>);

// ── every_offer_has_crash_branch_for_peer: whole-tree ────────────

// MPST-style composed protocol: first Offer from Alice with her
// crash branch, then an Offer from Bob with his crash branch,
// then End.  Alice-check and Bob-check both pass; Carol-check
// vacuously passes (neither Offer is from Carol).
using MpstCrashSafe = Offer<Sender<Alice>,
    Recv<Msg, Offer<Sender<Bob>,
        Recv<Ack, End>,
        Recv<Crash<Bob>, End>>>,
    Recv<Crash<Alice>, End>>;

static_assert(every_offer_has_crash_branch_for_peer_v<MpstCrashSafe, Alice>);
static_assert(every_offer_has_crash_branch_for_peer_v<MpstCrashSafe, Bob>);
static_assert(every_offer_has_crash_branch_for_peer_v<MpstCrashSafe, Carol>,
    "Neither Offer is from Carol — every-Offer predicate is vacuous");

// Alice-check fails if the Alice Offer drops its Crash<Alice> branch.
using MpstAliceMissing = Offer<Sender<Alice>,
    Recv<Msg, Offer<Sender<Bob>,
        Recv<Ack, End>,
        Recv<Crash<Bob>, End>>>,
    Recv<Ack, End>>;
static_assert(!every_offer_has_crash_branch_for_peer_v<MpstAliceMissing, Alice>);
// Bob-check still passes on MpstAliceMissing — Bob's Offer is intact.
static_assert(every_offer_has_crash_branch_for_peer_v<MpstAliceMissing, Bob>);

// ── compose preserves Sender<Role> ───────────────────────────────

// compose<Offer<Sender<Alice>, Send<Msg, End>, Send<Ack, End>>, Recv<Ack, End>>
//   == Offer<Sender<Alice>, Send<Msg, Recv<Ack, End>>, Send<Ack, Recv<Ack, End>>>
using ComposedAnn = compose_t<
    Offer<Sender<Alice>, Send<Msg, End>, Send<Ack, End>>,
    Recv<Ack, End>>;
using ExpectedComposedAnn = Offer<Sender<Alice>,
    Send<Msg, Recv<Ack, End>>,
    Send<Ack, Recv<Ack, End>>>;
static_assert(std::is_same_v<ComposedAnn, ExpectedComposedAnn>,
    "compose into annotated Offer must preserve Sender<Role>");

// ── compose_at_branch preserves Sender<Role> ─────────────────────

// Replace branch 1 (index 1) of an annotated Offer.
// Branches are counted WITHOUT the tag — so index 1 is "Send<Ack, End>".
using RewrittenAnn = compose_at_branch_t<
    Offer<Sender<Bob>, Send<Msg, End>, Send<Ack, End>>,
    /*I=*/ 1,
    Recv<Msg, End>>;
using ExpectedRewrittenAnn = Offer<Sender<Bob>,
    Send<Msg, End>,
    Send<Ack, Recv<Msg, End>>>;
static_assert(std::is_same_v<RewrittenAnn, ExpectedRewrittenAnn>,
    "compose_at_branch on annotated Offer indexes over real branches and preserves Sender<Role>");

// ── dual_of drops the sender tag (2-party dual semantics) ────────

using AnnDual = dual_of_t<Offer<Sender<Alice>, Recv<Msg, End>, Recv<Ack, End>>>;
using ExpectedAnnDual = Select<Send<Msg, End>, Send<Ack, End>>;
static_assert(std::is_same_v<AnnDual, ExpectedAnnDual>,
    "dual_of<Offer<Sender<Role>, ...>> drops the sender tag — MPST dual "
    "is role-dependent and handled by projection machinery, not 2-party dual");

// ── is_empty_choice: annotated-but-empty Offer is still empty ────

static_assert(is_empty_choice_v<Offer<>>,
    "Unannotated empty Offer is empty (existing behavior)");
static_assert(is_empty_choice_v<Offer<Sender<Alice>>>,
    "Annotated Offer with no branches is ALSO empty — no runnable "
    "handle should be constructible on it");

// ── is_well_formed: ignores the Sender<Role> tag ─────────────────

static_assert(is_well_formed_v<Offer<Sender<Alice>, Recv<Msg, End>, Recv<Ack, End>>>,
    "Well-formed annotated Offer stays well-formed");

}  // namespace

int main() {
    std::printf("test_session_sender_role: PASSED (compile-time witnesses).\n");
    return 0;
}
