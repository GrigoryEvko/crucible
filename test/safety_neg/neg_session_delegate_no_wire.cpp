// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: bare `delegate(handle)` (no transport) on a Delegate state.
// Per #369 the no-transport variant has been renamed to
// `delegate_local(handle)` and the bare `delegate(handle)` overload is
// `= delete`'d with `[Wire_Variant_Required]`.  Pre-#369 the bare form
// silently dropped the delegated endpoint — Delegate's semantic is
// "ship to peer", so omitting the transport meant the peer's
// corresponding Accept hung forever waiting for an endpoint that was
// never shipped.

#include <crucible/safety/Session.h>
#include <crucible/safety/SessionDelegate.h>

using namespace crucible::safety::proto;

struct R          { int sentinel = 1; };
struct DelegatedR { int sentinel = 2; };

using DelegatedProto = Send<int, End>;

void compile_time_reject() {
    // Carrier: Delegate<DelegatedProto, End> — Alice sends the
    // delegated endpoint to Bob, then advances to End.
    auto carrier  = make_session_handle<Delegate<DelegatedProto, End>>(R{});
    auto delegated = make_session_handle<DelegatedProto>(DelegatedR{});

    // Bare `.delegate(delegated_handle)` — pre-#369 silently dropped
    // the delegated endpoint; now a compile error.
    auto next = std::move(carrier).delegate(std::move(delegated));
    (void)next;
}

int main() { return 0; }
