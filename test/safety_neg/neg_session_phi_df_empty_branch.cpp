// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: assert_phi_df<P>() where P contains a structural
// deadlock — an empty Select<> with zero branches.  An empty choice
// is a stuck state under HYK24 reachability semantics: the sender
// has no decision to make, the peer has no expectation to receive.
//
// Per FX paper §11.18, phi_df strengthens phi_safe by rejecting
// such structural deadlock witnesses.  The substrate-side helper
// SessionPhi.h:assert_phi_df<P>() fires its classified static_assert
// with the framework-controlled prefix [PhiDfViolation_HasEmptyBranch].

#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionPhi.h>

using namespace crucible::safety::proto;

struct Request {};

// Protocol: send a Request, then offer an empty choice.  Well-formed
// (passes phi_safe) but the empty Select<> is an unreachable-progress
// state — phi_df refuses it.
using DeadlockedProto = Send<Request, Select<>>;

int main() {
    // Fires [PhiDfViolation_HasEmptyBranch] classified static_assert.
    assert_phi_df<DeadlockedProto>();
    return 0;
}
