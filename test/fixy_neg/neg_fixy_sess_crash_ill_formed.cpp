// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: `fscrash::CrashAwareForTransport<Loop<End>, Alice>` —
// the protocol is CRASH-VACUOUSLY-COVERED (no Offer<> on the tree,
// so the per-tree crash-branch walker passes by vacuous truth) but
// is ILL-FORMED: `Loop<End>` has a terminal-state body that can
// never reach `Continue`, rejected by Session.h's Loop<B>
// well-formedness gate.  The synthesis concept's two-part
// authorisation gates BOTH `is_well_formed_v` AND `every_offer_has_
// crash_branch_for_peer_v`, so an ill-formed-but-vacuously-covered
// protocol must REJECT.  This pins the §XXI discipline at the
// fixy:: re-export boundary: a future regression that weakened the
// concept to "crash-coverage only" would silently admit ill-formed
// protocols whose runtime behaviour is undefined.
//
// FIXY-V-064 HS14 floor — fixture 2 of 3.  Pairs with:
//   1. neg_fixy_sess_crash_oblivious_protocol.cpp
//      (CrashAwareForTransport rejects well-formed but crash-
//       oblivious clients — crash-coverage gate fires)
//   3. neg_fixy_sess_crash_assert_per_offer_missing.cpp
//      (assert_has_crash_branch_for fires on per-Offer level)
//
// This fixture's role: pin the well-formedness gate at the synthesis
// concept's authorisation point.  Without BOTH gates, the concept
// could admit a `Loop<End>` over a CrashWatched transport — the
// runtime would never make forward progress past the first loop
// iteration, masking real protocol-shape bugs as "crash-aware".

#include <crucible/fixy/SessCrash.h>

namespace fscrash = ::crucible::fixy::sess::crash;
namespace proto   = ::crucible::safety::proto;

namespace v064_neg_beta {
struct Alice {};

// `Loop<End>` is ill-formed: the body is terminal, so the loop's
// next iteration can never reach Continue.  Session.h rejects this
// via the is_terminal_state<B> check in is_well_formed<Loop<B>, _>.
// The protocol has NO Offer<> nodes, so the per-tree crash-branch
// walker passes vacuously — yet the synthesis concept must REJECT.
using IllFormedClient = proto::Loop<proto::End>;
}  // namespace v064_neg_beta

// Sanity: the crash-coverage walker passes vacuously (no Offer<>).
static_assert(fscrash::every_offer_has_crash_branch_for_peer_v<
    v064_neg_beta::IllFormedClient, v064_neg_beta::Alice>,
    "Sanity: Loop<End> has no Offer<> in its tree; the per-tree "
    "walker passes by vacuous truth.  Only the well-formedness "
    "gate must fire below.");

// Sanity: the protocol IS ill-formed (sanity check on the
// well-formedness gate's behaviour, distinct from the synthesis
// concept's rejection).
static_assert(!proto::is_well_formed_v<v064_neg_beta::IllFormedClient>,
    "Sanity: Loop<End> is ill-formed per Session.h's Loop<B> "
    "well-formedness rule (terminal body can never reach Continue).");

// Should FAIL: CrashAwareForTransport's well-formedness gate
// rejects.  GCC fires "static_assert ... IllFormedClient ... does
// not satisfy concept CrashAwareForTransport" or similar concept
// -diagnostic naming the synthesis concept and the rejecting
// well-formedness predicate.
static_assert(fscrash::CrashAwareForTransport<
    v064_neg_beta::IllFormedClient,
    v064_neg_beta::Alice>,
    "FIXY-V-064 fixture 2: CrashAwareForTransport must reject an "
    "ill-formed protocol even when crash-coverage is vacuously "
    "true — well-formedness is a non-vacuous, independent gate.");

int main() { return 0; }
