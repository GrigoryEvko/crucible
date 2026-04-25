// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: bare `select<I>()` (zero arguments) on a Select state.
// Per #377 the no-wire variant has been renamed to `select_local<I>()`
// and the bare `select<I>()` overload is `= delete`'d with the
// `[Wire_Variant_Required]` diagnostic — surfacing what was previously
// a silent footgun where wire-based sessions advanced the local
// handle without signalling the peer.

#include <crucible/sessions/Session.h>

using namespace crucible::safety::proto;

struct R { int sentinel = 1; };

void compile_time_reject() {
    auto h = make_session_handle<Select<Send<int, End>, Send<int, End>>>(R{});
    // Bare select<0>() — pre-#377 this silently selected the in-memory
    // variant; now it's a compile error pointing at the discipline.
    auto next = std::move(h).select<0>();
    (void)next;
}

int main() { return 0; }
