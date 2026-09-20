// Binary session-type core: the combinators, duality, well-formedness,
// composition, and the abandonment-check policy.
//
// Most of what this file checks is settled at compile time, because
// the protocol layer has no runtime representation.  The runtime main()
// is not decoration: a header whose every claim is a static_assert can
// be wrong in a way no static_assert reaches — the policy types carry
// state and move, and a self-move that clobbers the consumed flag is a
// runtime behaviour.  That one is exercised below for real.

#include <fixy/session/Stepping.h>

#include <cstdio>
#include <cstdlib>
#include <source_location>
#include <type_traits>
#include <utility>

namespace s = fixy::session;

namespace {

struct Alice {};
struct Msg {};
struct Ack {};

using PingPong = s::Send<Msg, s::Recv<Ack, s::End>>;
using PongPing = s::Recv<Msg, s::Send<Ack, s::End>>;

// ── Combinator shape ─────────────────────────────────────────────────

static_assert(s::is_send_v<PingPong>);
static_assert(!s::is_recv_v<PingPong>);
static_assert(s::is_recv_v<PongPing>);
static_assert(s::is_end_v<s::End>);
static_assert(s::is_continue_v<s::Continue>);
static_assert(s::is_loop_v<s::Loop<s::Send<Msg, s::Continue>>>);
static_assert(s::is_select_v<s::Select<s::End>>);
static_assert(s::is_offer_v<s::Offer<s::End>>);

static_assert(s::Select<s::End, s::End>::branch_count == 2);
// The Sender tag is an annotation, not a branch.  An Offer that
// counted it would misreport every branch index downstream.
static_assert(s::Offer<s::Sender<Alice>, s::End, s::End>::branch_count == 2);
static_assert(std::is_same_v<s::offer_sender_t<s::Offer<s::Sender<Alice>, s::End>>, Alice>);
static_assert(std::is_same_v<s::offer_sender_t<s::Offer<s::End>>, s::AnonymousPeer>);

// ── A handle can sit anywhere but on a Loop ──────────────────────────

static_assert(s::is_head_v<s::End>);
static_assert(s::is_head_v<PingPong>);
static_assert(!s::is_head_v<s::Loop<s::Send<Msg, s::Continue>>>);

// ── Duality ──────────────────────────────────────────────────────────

static_assert(std::is_same_v<s::dual_of_t<PingPong>, PongPing>);
static_assert(s::is_dual_v<PingPong, PongPing>);
static_assert(s::is_dual_v<PongPing, PingPong>);
static_assert(!s::is_dual_v<PingPong, PingPong>);

// Select faces Offer, never another Select.
static_assert(std::is_same_v<s::dual_of_t<s::Select<s::End>>, s::Offer<s::End>>);
static_assert(std::is_same_v<s::dual_of_t<s::Offer<s::End>>, s::Select<s::End>>);

// Duality is involutive everywhere except across a sender-annotated
// Offer, where the forward direction drops the tag and the reverse
// cannot restore it.  is_dual_v is a disjunction precisely so a
// channel pair on that shape still agrees from either side.
static_assert(s::is_dual_involutive_v<PingPong>);
static_assert(!s::is_dual_involutive_v<s::Offer<s::Sender<Alice>, s::End>>);
using OfferS = s::Offer<s::Sender<Alice>, s::Recv<Msg, s::End>>;
static_assert(s::is_dual_v<OfferS, s::dual_of_t<OfferS>>);
static_assert(!std::is_same_v<s::dual_of_t<s::dual_of_t<OfferS>>, OfferS>);

// ── Well-formedness ──────────────────────────────────────────────────

static_assert(s::is_well_formed_v<PingPong>);
// A Continue with no Loop above it is stuck: there is no state to loop
// back to.
static_assert(!s::is_well_formed_v<s::Continue>);
static_assert(!s::is_well_formed_v<s::Send<Msg, s::Continue>>);
static_assert(s::is_well_formed_v<s::Loop<s::Send<Msg, s::Continue>>>);
// A Loop whose whole body is terminal can never reach Continue — it is
// End wearing a loop's shape.
static_assert(!s::is_well_formed_v<s::Loop<s::End>>);
// The same terminal stays legal as one BRANCH of a loop body, because
// the other branch can still reach Continue.
static_assert(s::is_well_formed_v<s::Loop<s::Select<s::Send<Msg, s::Continue>, s::End>>>);

// ── Empty choice ─────────────────────────────────────────────────────
//
// An empty Select is a legitimate type operand under branch covariance
// but is not runnable: there is no branch to pick.  The trait exists so
// handle construction can refuse it while subtyping keeps admitting it.

static_assert(s::is_empty_choice_v<s::Select<>>);
static_assert(s::is_empty_choice_v<s::Offer<>>);
static_assert(s::is_empty_choice_v<s::Offer<s::Sender<Alice>>>);
static_assert(!s::is_empty_choice_v<s::Select<s::End>>);
// The walk is recursive: an empty choice buried behind a Send is still
// a dead end, and rejecting it only at the top would surface the
// misuse at the eventual operation instead of at construction.
static_assert(s::is_empty_choice_v<s::Send<Msg, s::Select<>>>);
static_assert(s::is_empty_choice_v<s::Loop<s::Recv<Msg, s::Offer<>>>>);

// ── Composition ──────────────────────────────────────────────────────

static_assert(std::is_same_v<s::compose_t<s::Send<Msg, s::End>, s::Recv<Ack, s::End>>,
                             s::Send<Msg, s::Recv<Ack, s::End>>>);
// Continue marks a loop-back, not an end, so composition leaves it be.
static_assert(std::is_same_v<s::compose_t<s::Continue, s::End>, s::Continue>);
// Uniform composition reaches every branch.
static_assert(std::is_same_v<s::compose_t<s::Select<s::End, s::End>, s::Recv<Ack, s::End>>,
                             s::Select<s::Recv<Ack, s::End>, s::Recv<Ack, s::End>>>);
// The sender tag survives and is not treated as a branch to compose into.
static_assert(std::is_same_v<s::compose_t<s::Offer<s::Sender<Alice>, s::End>, s::End>,
                             s::Offer<s::Sender<Alice>, s::End>>);

// Branch-asymmetric composition touches one branch and leaves the rest.
static_assert(std::is_same_v<s::compose_at_branch_t<s::Select<s::End, s::End>, 0, s::Recv<Ack, s::End>>,
                             s::Select<s::Recv<Ack, s::End>, s::End>>);
// It walks the spine through Send to reach the choice.
static_assert(std::is_same_v<s::compose_at_branch_t<s::Send<Msg, s::Select<s::End, s::End>>, 1, s::Recv<Ack, s::End>>,
                             s::Send<Msg, s::Select<s::End, s::Recv<Ack, s::End>>>>);

// ── Vendor pinning is transparent to structure ───────────────────────

using Pinned = s::VendorPinned<s::VendorBackend::NV, PingPong>;
static_assert(s::is_vendor_pinned_v<Pinned>);
static_assert(s::protocol_vendor_v<Pinned> == s::VendorBackend::NV);
static_assert(s::protocol_vendor_v<PingPong> == s::VendorBackend::Portable);
// Every structural question sees through the wrapper.
static_assert(s::is_send_v<Pinned>);
static_assert(s::is_well_formed_v<Pinned>);
static_assert(std::is_same_v<s::dual_of_t<Pinned>, s::VendorPinned<s::VendorBackend::NV, PongPing>>);

// ── Terminality ──────────────────────────────────────────────────────

static_assert(s::is_terminal_state_v<s::End>);
static_assert(!s::is_terminal_state_v<PingPong>);
static_assert(s::is_terminal_state_v<s::VendorPinned<s::VendorBackend::NV, s::End>>);

// ── The abandonment-check policy ─────────────────────────────────────

static_assert(s::AbandonmentPolicy<s::check::Enforced>);
static_assert(s::AbandonmentPolicy<s::check::Off>);

static_assert(s::check::Enforced::checks_abandonment);
static_assert(!s::check::Off::checks_abandonment);

// Off must stay collapsible or a handle under it stops costing exactly
// its Resource.
static_assert(std::is_empty_v<s::check::Off>);
static_assert(!std::is_empty_v<s::check::Enforced>);

// The policy is readable without asking about the build mode.  This
// test target compiles with -UNDEBUG, so the default here enforces —
// and the assertion is written against the policy rather than against
// NDEBUG, so it keeps meaning if the default is ever re-decided.
static_assert(s::default_policy_checks_abandonment == s::DefaultAbandonmentPolicy::checks_abandonment);
static_assert(std::is_same_v<s::DefaultAbandonmentPolicy, s::check::Enforced>,
              "test targets compile -UNDEBUG, so the default policy must be the enforcing one.  If this fires, "
              "the one switch in fixy/session/Stepping.h moved and every test that relies on abandonment being "
              "caught is now silently unchecked.");

}  // namespace

int main() {
    // A default-constructed tracker is unmarked: a fresh handle owes
    // its protocol.
    s::check::Enforced fresh{};
    if (fresh.was_marked()) {
        std::fprintf(stderr, "a fresh Enforced tracker reports consumed\n");
        return 1;
    }
    fresh.mark();
    if (!fresh.was_marked()) {
        std::fprintf(stderr, "mark() did not take\n");
        return 1;
    }

    // Moving marks the SOURCE consumed, so the moved-from handle's
    // destructor skips the abort while the destination owes the step.
    s::check::Enforced src{};
    s::check::Enforced dst{};
    dst.move_from(src);
    if (!src.was_marked()) {
        std::fprintf(stderr, "move_from left the source owing its protocol\n");
        return 1;
    }
    if (dst.was_marked()) {
        std::fprintf(stderr, "move_from marked the destination consumed\n");
        return 1;
    }

    // Self-move must leave the flag untouched.  Without the guard this
    // marks a live handle consumed and the abandonment check stops
    // firing for a protocol that really was dropped.  No static_assert
    // reaches this: it is an aliasing behaviour.
    s::check::Enforced self{};
    self.move_from(self);
    if (self.was_marked()) {
        std::fprintf(stderr, "self-move marked a live tracker consumed — the abandonment check is now disabled "
                             "for this handle\n");
        return 1;
    }

    // The location round-trips, which is what makes the destructor's
    // diagnostic point at the caller rather than at the framework.
    const auto here = std::source_location::current();
    const s::check::Enforced located{here};
    if (located.construction_loc().line() != here.line()) {
        std::fprintf(stderr, "construction location did not round-trip\n");
        return 1;
    }

    // Off answers consumed to everything, which is what folds the
    // destructor check away.
    s::check::Off off{};
    if (!off.was_marked()) {
        std::fprintf(stderr, "check::Off must report consumed so the destructor check folds away\n");
        return 1;
    }

    return 0;
}
