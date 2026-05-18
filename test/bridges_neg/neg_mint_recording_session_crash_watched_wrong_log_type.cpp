// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-016: HS14 floor for mint_recording_session(CrashWatchedHandle, ...).
//
// Distinct mismatch class from neg_mint_recording_session_crash_watched_non_handle.cpp:
// the inbound first argument is a real SessionEventLog (a real Crucible
// class), but it is NOT a CrashWatchedHandle specialisation and not a
// SessionHandle either.  The diagnostic should explicitly state no
// overload of mint_recording_session matches the call shape.
//
// Expected diagnostic:
//   "no matching function for call to 'mint_recording_session'"

#include <crucible/bridges/RecordingSessionHandle.h>

namespace proto = ::crucible::safety::proto;

int main() {
    // Pass a SessionEventLog where a handle is expected.  All three
    // mint_recording_session overloads require their first parameter to
    // be a SessionHandle / CrashWatchedHandle / PSH; SessionEventLog is
    // none of those.
    proto::SessionEventLog log{};
    proto::RoleTagId       self{1};
    proto::RoleTagId       peer{2};

    auto bad = proto::mint_recording_session(log, log, self, peer);
    (void)bad;
    return 0;
}
