// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-CR-14: `is_empty_choice_v<P>` now walks recursively through
// every reachable position in P.  This fixture is the canonical
// witness — `Loop<Recv<int, Select<>>>` is well-formed at the
// type level (no free Continue, Loop body is valid) but contains
// a reachable empty Select<> nested inside Recv's continuation
// inside Loop's body.  Before CR-14, mint_session_handle accepted
// this protocol; the runtime then crashed when the user tried to
// `.pick<I>()` on the dead-end Select after consuming the Recv.
// After CR-14, mint rejects at construction time.
//
// Pairs with neg_session_empty_select.cpp (which covers the bare
// top-level `Select<>` case — exercises the explicit empty
// specialization).  This fixture exercises the new recursive
// specializations (Send/Recv/Loop walk).
//
// Expected diagnostic: [Empty_Choice_Combinator] — the same
// catalog tag the bare empty-Select fixture targets; the only
// difference is the path through which the trait reaches the
// dead end.

#include <crucible/sessions/Session.h>

using namespace crucible::safety::proto;

struct R { int sentinel = 1; };

void compile_time_reject() {
    using NestedEmptyProto = Loop<Recv<int, Select<>>>;
    auto h = mint_session_handle<NestedEmptyProto>(R{});
    (void)h;
}

int main() { return 0; }
