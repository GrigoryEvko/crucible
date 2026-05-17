// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-CR-14: a SECOND witness for recursive `is_empty_choice_v`,
// distinct from the Loop<Recv<...>> shape — `Send<int, Offer<>>`
// has empty Offer<> nested directly inside Send's continuation.
// Exercises the recursive Send → continuation walk specifically;
// the previous fixture goes through Loop → Recv.
//
// Also distinct from the bare top-level Offer<> fixture: the
// guard fires only because the recursive walk visits Send's K.
//
// Expected diagnostic: [Empty_Choice_Combinator].

#include <crucible/sessions/Session.h>

using namespace crucible::safety::proto;

struct R { int sentinel = 2; };

void compile_time_reject() {
    using NestedEmptyProto = Send<int, Offer<>>;
    auto h = mint_session_handle<NestedEmptyProto>(R{});
    (void)h;
}

int main() { return 0; }
