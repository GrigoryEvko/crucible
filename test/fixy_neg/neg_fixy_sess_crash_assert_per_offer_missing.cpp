// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: `fscrash::assert_has_crash_branch_for<NormalOffer,
// Alice>()` — the per-Offer consteval assertion helper fires when
// the Offer<> lacks a `Recv<Crash<Alice>, _>` branch.  The substrate
// ships TWO surfaces for crash-branch checking: a predicate
// (`has_crash_branch_for_peer_v`) and a consteval assertion
// (`assert_has_crash_branch_for`).  This fixture pins the consteval
// helper at the fixy:: re-export boundary — distinct diagnostic
// surface from the synthesis-concept rejection in fixtures 1 & 2,
// because `assert_has_crash_branch_for` fires a single static_assert
// inside a consteval function body (NOT a concept-overload
// diagnostic), exposing the named-call-site discipline for callers
// that demand a specific per-Offer crash-handling contract.
//
// FIXY-V-064 HS14 floor — fixture 3 of 3.  Pairs with:
//   1. neg_fixy_sess_crash_oblivious_protocol.cpp
//      (CrashAwareForTransport concept rejects at the tree level)
//   2. neg_fixy_sess_crash_ill_formed.cpp
//      (CrashAwareForTransport concept rejects at well-formedness)
//
// This fixture's role: pin the per-Offer named-call-site consteval
// assertion at the fixy:: re-export boundary.  Without it, the
// substrate could re-route `assert_has_crash_branch_for` into a
// no-op (or a deferred-runtime check) and production call sites
// would silently lose the per-Offer compile-time guarantee — masking
// programmer errors that miss a crash branch on a SPECIFIC Offer<>
// rather than on the entire tree.

#include <crucible/fixy/SessCrash.h>

namespace fscrash = ::crucible::fixy::sess::crash;
namespace proto   = ::crucible::safety::proto;

namespace v064_neg_gamma {
struct Alice {};
struct Msg   {};
struct Ack   {};

// An Offer<> with NO Crash<Alice> branch.  Distinct from fixture 1's
// CrashOblivClient (which wraps NormalOffer in Send<Msg, ...>) —
// this fixture targets the BARE-OFFER consteval-assertion path that
// individual call sites use when demanding a per-Offer contract.
using NormalOffer = proto::Offer<
    proto::Recv<Msg, proto::End>,
    proto::Recv<Ack, proto::End>>;
}  // namespace v064_neg_gamma

// Sanity: the per-Offer predicate returns false (sanity check
// distinct from the consteval assertion's diagnostic).
static_assert(!fscrash::has_crash_branch_for_peer_v<
    v064_neg_gamma::NormalOffer, v064_neg_gamma::Alice>,
    "Sanity: NormalOffer lacks Recv<Crash<Alice>, _>; the predicate "
    "returns false (consteval assertion must fire below).");

// Force consteval evaluation of the assertion helper at a non-
// consteval call site.  The substrate's `assert_has_crash_branch
// _for<Offer, Peer>()` is `consteval void` — calling it from a
// constexpr context forces evaluation, firing the embedded
// static_assert when the Offer lacks the requested crash branch.
constexpr bool force_consteval_eval = [] consteval {
    // Should FAIL: NormalOffer lacks Crash<Alice>; the embedded
    // static_assert inside assert_has_crash_branch_for's body fires
    // with the diagnostic naming `CrashBranch_Missing` per
    // SessionCrash.h:506.
    fscrash::assert_has_crash_branch_for<
        v064_neg_gamma::NormalOffer,
        v064_neg_gamma::Alice>();
    return true;
}();

int main() {
    (void) force_consteval_eval;
    return 0;
}
