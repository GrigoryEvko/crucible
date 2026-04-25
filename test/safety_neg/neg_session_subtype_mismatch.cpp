// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: assert_subtype_sync on two incompatible protocols —
// SessionSubtype.h's consteval helper fires its classified
// static_assert.

#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionSubtype.h>

using namespace crucible::safety::proto;

// Send and Recv are different combinator shapes — no subtype relation
// in either direction.
using ProtoA = Send<int, End>;
using ProtoB = Recv<int, End>;

int main() {
    assert_subtype_sync<ProtoA, ProtoB>();  // fires static_assert
    return 0;
}
