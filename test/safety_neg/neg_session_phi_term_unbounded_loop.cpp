// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: assert_phi_term<P>() where P contains a Loop<B> whose
// body B has no path reaching End or Stop.  Every branch returns to
// Continue, so the loop is structurally inescapable — productive but
// non-terminating.
//
// Per FX paper §11.18, phi_term strengthens phi_df by additionally
// rejecting protocols with unbounded loops (loop_body_terminates_v<B>
// false for some enclosed Loop<B>).  The substrate-side helper
// SessionPhi.h:assert_phi_term<P>() fires its classified static_assert
// with the framework-controlled prefix
// [PhiTermViolation_HasUnboundedLoop].

#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionPhi.h>

using namespace crucible::safety::proto;

struct Heartbeat {};
struct Ack       {};

// Protocol: Loop sending Heartbeat, receiving Ack, Continue.  Every
// path returns to Continue — there is no End/Stop branch — so the
// loop body cannot escape.  phi_safe accepts it (well-formed) but
// phi_term refuses it.
using NeverEndingHeartbeat =
    Loop<Send<Heartbeat, Recv<Ack, Continue>>>;

int main() {
    // Fires [PhiTermViolation_HasUnboundedLoop] classified static_assert.
    assert_phi_term<NeverEndingHeartbeat>();
    return 0;
}
