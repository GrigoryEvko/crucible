// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: global type with Transmission<X, X, ...> — a participant
// cannot send to itself in MPST (#363).  assert_no_self_loop<G>()
// emits the routed [ProtocolViolation_Self_Loop] diagnostic.

#include <crucible/safety/SessionGlobal.h>

using namespace crucible::safety::proto;

struct Alice {};
struct Ping {};

// Self-transmission — Alice sends to Alice.  Ill-formed.
using IllFormedG = Transmission<Alice, Alice, Ping, End_G>;

int main() {
    // Fires the framework-controlled [ProtocolViolation_Self_Loop]
    // tag prefix; stable across GCC versions.
    assert_no_self_loop<IllFormedG>();
    return 0;
}
