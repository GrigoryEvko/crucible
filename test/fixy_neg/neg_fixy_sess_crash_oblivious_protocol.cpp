// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: `fscrash::CrashAwareForTransport<CrashOblivClient,
// Alice>` — the protocol is WELL-FORMED but lacks a `Recv<Crash
// <Alice>, _>` branch in its single Offer<>.  The synthesis
// concept's two-part gate combines `is_well_formed_v` (passes) with
// `every_offer_has_crash_branch_for_peer_v` (FAILS) — the per-tree
// crash-coverage gate fires.  This pins the §XXI discipline at the
// fixy:: re-export boundary: a future regression that weakened the
// concept to "is_well_formed_v only" would silently admit crash-
// oblivious protocols over a CrashWatched transport, ABANDONING the
// handle at runtime when the peer crashes.
//
// FIXY-V-064 HS14 floor — fixture 1 of 3.  Pairs with:
//   2. neg_fixy_sess_crash_ill_formed.cpp
//      (CrashAwareForTransport rejects ill-formed protocols even
//       when crash branches are present)
//   3. neg_fixy_sess_crash_assert_per_offer_missing.cpp
//      (assert_has_crash_branch_for fires on per-Offer level)
//
// This fixture's role: pin the per-tree-walker gate at the synthesis
// concept's authorisation point.  Without it, observe metrics
// claiming "this session is crash-aware" could rely on well-
// formedness alone — a strictly weaker discipline that admits
// every protocol with an Offer<> missing the Crash<Peer> branch.

#include <crucible/fixy/SessCrash.h>

namespace fscrash = ::crucible::fixy::sess::crash;
namespace proto   = ::crucible::safety::proto;

namespace v064_neg_alpha {
struct Alice {};
struct Msg   {};
struct Ack   {};

// Well-formed but CRASH-OBLIVIOUS: the Offer<> has Msg / Ack
// branches but NO Recv<Crash<Alice>, _> branch.  A CrashWatched
// transport receiving a peer-crash signal here would have nowhere
// to dispatch it.
using NormalOffer = proto::Offer<
    proto::Recv<Msg, proto::End>,
    proto::Recv<Ack, proto::End>>;
using CrashOblivClient = proto::Send<Msg, NormalOffer>;
}  // namespace v064_neg_alpha

// Sanity: the protocol IS well-formed (the well-formedness gate
// passes on its own — only the crash-coverage gate should fire).
static_assert(proto::is_well_formed_v<v064_neg_alpha::CrashOblivClient>,
    "Sanity: NormalOffer-wrapping client is well-formed; only the "
    "crash-coverage gate must fire below.");

// Should FAIL: CrashAwareForTransport's per-tree walker rejects.
// GCC fires "static_assert ... CrashOblivClient ... does not
// satisfy concept CrashAwareForTransport" or similar concept-
// diagnostic naming the synthesis concept and the rejecting tree
// walker.
static_assert(fscrash::CrashAwareForTransport<
    v064_neg_alpha::CrashOblivClient,
    v064_neg_alpha::Alice>,
    "FIXY-V-064 fixture 1: CrashAwareForTransport must reject a "
    "well-formed but crash-oblivious client — well-formedness alone "
    "is strictly weaker than crash-aware transport discipline.");

int main() { return 0; }
