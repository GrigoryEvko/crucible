// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: global type with Var_G outside any enclosing Rec_G.
// L4 SessionGlobal.h's is_global_well_formed_v rejects — used via
// the is_well_formed check baked into projection.

#include <crucible/safety/Session.h>
#include <crucible/safety/SessionGlobal.h>

using namespace crucible::safety::proto;

struct Alice {};
struct Bob   {};

// Var_G at the end — no enclosing Rec_G.  Ill-formed.
using IllFormedG = Transmission<Alice, Bob, int, Var_G>;

int main() {
    static_assert(is_global_well_formed_v<IllFormedG>,
        "should be ill-formed");
    return 0;
}
