// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-023: companion to neg_refines_self_double_dual_sender_offer.cpp.
// Witnesses that `refines_self_and_double_dual_v<P>` REJECTS protocols
// containing a Sender-annotated Offer NESTED inside an outer involutive
// shape — here `Loop<Offer<Sender<Role>, ...>>`.  The Loop wrapper IS
// involutive, but `is_dual_involutive_v` propagates conservatively
// (Session.h:710 + :707): any subterm with the asymmetric
// specialization poisons the whole protocol, so the trait must report
// false even though the OUTERMOST shape is structurally well-behaved.
//
// This is the load-bearing witness for the conservative propagation
// rule.  Without the `is_dual_involutive_v<P>` gate added at
// SessionPatterns.h:944+, a nested Sender-Offer could slip past the
// subtype-level check (the `is_subtype_sync_v<P, dual(dual(P))>`
// conjunct might return some value, but the value is semantically
// vacuous — dual(dual(P)) is structurally different from P).
//
// Expected diagnostic family (one or more should match):
//   "static assertion failed"  |  "refines_self_and_double_dual"  |
//   "is_dual_involutive"

#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionPatterns.h>

namespace proto = ::crucible::safety::proto;

struct DaveRole {};
struct Tick {};

using LoopOverSenderOffer = proto::Loop<
    proto::Offer<proto::Sender<DaveRole>,
                 proto::Recv<Tick, proto::Continue>>>;

static_assert(
    proto::refines_self_and_double_dual_v<LoopOverSenderOffer>,
    "fixy-A2-023: a Loop containing a Sender-annotated Offer is "
    "structurally non-involutive (the inner specialization at "
    "Session.h:707 reports false, which propagates).  The trait must "
    "reject it; reaching this static_assert means the gate at "
    "SessionPatterns.h:944+ was bypassed.");

int main() { return 0; }
