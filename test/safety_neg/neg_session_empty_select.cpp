// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: `mint_session_handle<Select<>>(r)` — constructing a
// runnable handle on an empty Select<>.  Per #364 this is rejected
// at the handle boundary with `[Empty_Choice_Combinator]` while
// preserving subtyping-level uses of Select<> (Gay-Hole 2005's
// minimum-subtype rule remains valid; only handle reification is
// rejected).

#include <crucible/sessions/Session.h>

using namespace crucible::safety::proto;

struct R { int sentinel = 1; };

void compile_time_reject() {
    // Empty Select<> at the handle boundary — mint_session_handle's
    // static_assert fires with [Empty_Choice_Combinator].
    auto h = mint_session_handle<Select<>>(R{});
    (void)h;
}

int main() {
    return 0;
}
