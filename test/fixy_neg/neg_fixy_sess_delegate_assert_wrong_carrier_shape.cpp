// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: `fsdelegate::assert_delegates_to<C, T>()` where C is
// structurally NOT a Delegate<T, _>.  Here we pass a Send<T, K> as the
// carrier — Send moves a VALUE of T; Delegate moves an ENDPOINT
// speaking T.  The two are structurally distinct combinators and the
// concept `DelegatesTo<C, T>` must reject Send.
//
// FIXY-V-060 HS14 floor — fixture 1 of 3.  Pairs with:
//   2. neg_fixy_sess_delegate_assert_wrong_accept_carrier.cpp
//      (dual-side: Recv instead of Accept)
//   3. neg_fixy_sess_delegate_crash_must_abort.cpp
//      (crash propagation regression: T can fail but K has no
//      Crash<RecipientTag> branch)
//
// This fixture's role: pin the most common Delegate-vs-Send confusion
// at the consteval assertion site.  A buggy refactor that swaps a
// Delegate combinator for a Send (forgetting that the protocol-typed
// payload semantics differ from value-typed) is rejected AT the
// assert_delegates_to site, with the diagnostic naming Carrier vs
// Delegated rather than three TUs deep in SessionHandle::delegate's
// transport-callable deduction.

#include <crucible/fixy/SessDelegate.h>
#include <crucible/sessions/Session.h>

namespace fsdelegate = ::crucible::fixy::sess::delegate;
namespace proto      = ::crucible::safety::proto;

namespace {
struct Req {};
struct Ack {};

// The delegated protocol — what the carrier IS purportedly handing off.
using T_delegated = proto::Send<Req, proto::End>;

// The continuation after the handoff.
using K_continue = proto::End;

// WRONG: a Send<T, K>, not a Delegate<T, K>.  Send moves a value of T,
// not an endpoint speaking T.  Concept DelegatesTo<C, T> must reject.
using WrongCarrier = proto::Send<T_delegated, K_continue>;
}  // namespace

int main() {
    // Should FAIL: WrongCarrier is Send<T, K> — not Delegate<T, K>.
    // assert_delegates_to fires the ProtocolViolation_State
    // diagnostic at this call site, pinning the carrier-shape
    // mismatch.
    fsdelegate::assert_delegates_to<WrongCarrier, T_delegated>();
    return 0;
}
