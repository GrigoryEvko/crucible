// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-016: HS14 floor for mint_recording_session(SessionHandle, ...).
//
// Token mint whose authority is the inbound SessionHandle<P, R, L>.
// The first parameter type IS the gate — non-handle inputs fail
// substitution because no overload of the factory accepts bare `int`.
// (The CrashWatchedHandle and PSH overloads also reject `int`.)
//
// Expected diagnostic:
//   "no matching function for call to 'mint_recording_session'"

#include <crucible/bridges/RecordingSessionHandle.h>
#include <crucible/sessions/SessionEventLog.h>

namespace proto = ::crucible::safety::proto;

int main() {
    proto::SessionEventLog log{};
    int                    not_a_handle = 42;
    proto::RoleTagId       self{1};
    proto::RoleTagId       peer{2};

    auto bad = proto::mint_recording_session(not_a_handle, log, self, peer);
    (void)bad;
    return 0;
}
