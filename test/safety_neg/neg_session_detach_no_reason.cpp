// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: SessionHandle::detach() called WITHOUT a typed reason
// tag from detach_reason::*.  Per #376 the bare zero-arg overload is
// `= delete("...")` with a framework-controlled diagnostic carrying
// the [DetachReason_Required] audit prefix.

#include <crucible/sessions/Session.h>

#include <utility>

using namespace crucible::safety::proto;

struct R { int sentinel = 1; };

int main() {
    auto h = make_session_handle<End>(R{});
    // Detaching without a reason tag — the deleted overload outranks
    // the templated detach<Reason>(Reason) for a zero-arg call, so
    // GCC fires the framework-controlled "[DetachReason_Required] ..."
    // string.  Test PASSES iff the diagnostic includes that prefix.
    std::move(h).detach();
    return 0;
}
