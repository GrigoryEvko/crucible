// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-023: witness that `refines_self_and_double_dual_v<P>`
// REJECTS protocols whose `dual_of` is non-involutive — specifically,
// `Offer<Sender<Role>, Bs...>` (Session.h:646).  Per fixy-CR-11 the
// dual strips the Sender annotation, so `dual_of_t<dual_of_t<P>>` is
// `Offer<Bs...>` (no Sender), a STRUCTURALLY DIFFERENT protocol.
//
// The trait's "P refines its own double dual" claim presupposes
// involution — without it, the RHS is not the involution and the
// answer carries no subtype-lattice meaning.  The fix at
// SessionPatterns.h:944+ adds an `is_dual_involutive_v<P>` conjunct
// so this case is statically rejected; this fixture pins HS14 by
// witnessing the rejection rather than relying on a far-away
// downstream surface to surface it.
//
// Expected diagnostic family (one or more should match):
//   "static assertion failed"  |  "refines_self_and_double_dual"  |
//   "is_dual_involutive"

#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionPatterns.h>

namespace proto = ::crucible::safety::proto;

struct CarolRole {};
struct Payload {};

using SenderOfferProto =
    proto::Offer<proto::Sender<CarolRole>,
                 proto::Recv<Payload, proto::End>>;

static_assert(proto::refines_self_and_double_dual_v<SenderOfferProto>,
    "fixy-A2-023: SenderOfferProto is NOT dual-involutive — the trait "
    "must reject it.  Reaching this static_assert means the gate at "
    "SessionPatterns.h:944+ was bypassed.");

int main() { return 0; }
