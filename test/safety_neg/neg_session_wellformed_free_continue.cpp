// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: using make_session_handle<Proto> with Proto containing
// a free Continue (outside any enclosing Loop).  L1 Session.h's
// well-formedness check in make_session_handle rejects this.

#include <crucible/safety/Session.h>

using namespace crucible::safety::proto;

// Ill-formed protocol: Continue not inside a Loop.
using IllFormedProto = Send<int, Continue>;

struct FakeRes {};

int main() {
    // make_session_handle enforces is_well_formed_v — fires a static_assert.
    auto h = make_session_handle<IllFormedProto>(FakeRes{});
    (void)h;
    return 0;
}
