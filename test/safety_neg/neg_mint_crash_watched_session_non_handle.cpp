// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CLAUDE.md §XXI Universal Mint Pattern + HS14: every mint factory
// ships at least 2 negative-compile fixtures demonstrating the
// soundness gate fires.
//
// mint_crash_watched_session<PeerTag>(handle, flag) is a TOKEN MINT
// whose authority is the inbound SessionHandle<Proto, Resource,
// LoopCtx>.  The first parameter type IS the gate — non-handle inputs
// fail substitution (no matching function template).
//
// Violation: pass an int as the "handle" argument.  The mint's first
// parameter requires a SessionHandle specialisation; the substitution
// fails before any constraint is checked.
//
// Expected diagnostic:
//   "no matching function for call to 'mint_crash_watched_session<...>'"
//   "could not match 'SessionHandle<...>' against 'int'"

#include <crucible/bridges/CrashTransport.h>

namespace proto = crucible::safety::proto;
using crucible::safety::OneShotFlag;

struct ServerPeer {};

int main() {
    OneShotFlag flag;
    int         not_a_handle = 42;

    // The mint requires its first argument to be a SessionHandle
    // specialisation; an int can never bind to that parameter slot.
    auto bad = proto::mint_crash_watched_session<ServerPeer>(
        not_a_handle, flag);
    (void)bad;
    return 0;
}
