// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-058 fixture #4 — role-distinctness invariant for a global
// carrier that tries to send a delegated endpoint to itself.

#include <crucible/sessions/PermissionedSession.h>

using namespace crucible::safety::proto;

struct CarrierRole {};
struct WorkItem {};

using InnerProto = Send<int, End>;
using Payload    = DelegatedSession<InnerProto, PermSet<WorkItem>>;

// Self-transmission of a DelegatedSession payload.  MPST projection
// cannot assign this to distinct Delegate/Accept endpoints.
using SelfDelegatingGlobal =
    Transmission<CarrierRole, CarrierRole, Payload, End_G>;

int main() {
    assert_no_self_loop<SelfDelegatingGlobal>();
    return 0;
}
