// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: `fsdelegate::assert_delegated_crash_propagates<T, R, K>()`
// where T contains structural Send/Select/Delegate (so the recipient
// could fail mid-protocol) but K does NOT offer a Crash<R> recovery
// branch.  HYC08 + crash-stop classifies this as `MustAbort` — the
// carrier's continuation cannot recover from a recipient crash.
//
// FIXY-V-060 HS14 floor — fixture 3 of 3.  Pairs with:
//   1. neg_fixy_sess_delegate_assert_wrong_carrier_shape.cpp
//      (carrier-shape: Send instead of Delegate)
//   2. neg_fixy_sess_delegate_assert_wrong_accept_carrier.cpp
//      (carrier-shape: Recv instead of Accept on the dual side)
//
// This fixture's role: catch the crash-propagation regression class
// where a refactor adds a Delegate handoff to a pipeline whose K
// terminates cleanly (End) without offering a Crash<RecipientTag>
// branch.  T_delegated CAN fail mid-protocol (it contains a Send),
// so the recipient may crash before the Send is observed — and the
// carrier has no recovery option.  The substrate fires the
// [DelegatedCrashPropagation_MissingRecovery] diagnostic.
//
// Without this gate, a future refactor that drops a Crash branch
// from an existing K (or adds a new Delegate where K hasn't been
// updated) would silently leak the unrecoverable obligation through
// to runtime — the entire point of crash-propagation classification
// is to fire AT the protocol-declaration site before the system is
// ever launched.

#include <crucible/fixy/SessDelegate.h>
#include <crucible/sessions/Session.h>

namespace fsdelegate = ::crucible::fixy::sess::delegate;
namespace proto      = ::crucible::safety::proto;

namespace {
struct Req {};
struct RecipientTag {};

// T = Send<Req, End> — contains a Send, so T CAN emit before
// terminating.  Recipient that crashes before the Send completes
// leaves the carrier in an unrecoverable state.
using T_delegated = proto::Send<Req, proto::End>;

// K = End — terminates cleanly; NO Offer with Crash<RecipientTag>
// branch.  This is the crash-propagation failure shape.
using K_continue = proto::End;
}  // namespace

int main() {
    // Should FAIL: delegated_crash_propagation_t classifies this
    // (T, R, K) triple as `MustAbort`.  assert_delegated_crash_
    // propagates fires the
    // [DelegatedCrashPropagation_MissingRecovery] diagnostic — the
    // carrier must add a Crash<RecipientTag> recovery branch to K
    // (or T must be a Recv-only chain) before this assertion can
    // pass.
    fsdelegate::assert_delegated_crash_propagates<
        T_delegated, RecipientTag, K_continue>();
    return 0;
}
