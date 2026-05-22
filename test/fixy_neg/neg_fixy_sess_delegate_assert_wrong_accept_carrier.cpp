// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: `fsdelegate::assert_accepts_from<C, T>()` where C is
// structurally NOT an Accept<T, _>.  Here we pass a Recv<T, K> as the
// carrier — Recv receives a VALUE of T; Accept receives an ENDPOINT
// speaking T.  The two are structurally distinct combinators and the
// concept `AcceptsFrom<C, T>` must reject Recv.
//
// FIXY-V-060 HS14 floor — fixture 2 of 3.  Pairs with:
//   1. neg_fixy_sess_delegate_assert_wrong_carrier_shape.cpp
//      (dual-side: Send instead of Delegate)
//   3. neg_fixy_sess_delegate_crash_must_abort.cpp
//      (crash propagation regression: T can fail but K has no
//      Crash<RecipientTag> branch)
//
// This fixture's role: pin the Accept-vs-Recv side of the same
// payload-confusion regression family.  The dual-side coverage
// matters because the substrate's `DelegatesTo` and `AcceptsFrom`
// concepts route through different specializations of
// `delegated_proto` extraction; a future regression that conflates
// them would slip through if only the Delegate side were pinned.

#include <crucible/fixy/SessDelegate.h>
#include <crucible/sessions/Session.h>

namespace fsdelegate = ::crucible::fixy::sess::delegate;
namespace proto      = ::crucible::safety::proto;

namespace {
struct Req {};
struct Ack {};

// The delegated protocol — what the carrier purportedly receives.
using T_delegated = proto::Send<Req, proto::End>;

// The continuation after the handoff.
using K_continue = proto::End;

// WRONG: a Recv<T, K>, not an Accept<T, K>.  Recv receives a value of
// T, not an endpoint speaking T.  Concept AcceptsFrom<C, T> must
// reject.
using WrongCarrier = proto::Recv<T_delegated, K_continue>;
}  // namespace

int main() {
    // Should FAIL: WrongCarrier is Recv<T, K> — not Accept<T, K>.
    // assert_accepts_from fires the ProtocolViolation_State
    // diagnostic at this call site, pinning the carrier-shape
    // mismatch on the dual side.
    fsdelegate::assert_accepts_from<WrongCarrier, T_delegated>();
    return 0;
}
