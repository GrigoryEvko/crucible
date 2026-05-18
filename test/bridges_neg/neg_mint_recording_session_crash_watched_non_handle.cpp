// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-016: HS14 floor for mint_recording_session(CrashWatchedHandle, ...).
//
// The CrashWatchedHandle overload of mint_recording_session is distinct
// from the bare-SessionHandle overload — its parameter is
// CrashWatchedHandle<P, R, PT, C, L, PS>.  Passing a struct that is NOT
// a CrashWatchedHandle specialisation should fall through to NO
// matching function (the bare-SessionHandle overload also rejects
// non-SessionHandle inputs).
//
// Violation: pass a bespoke struct that is neither a SessionHandle nor
// a CrashWatchedHandle.  Both overloads fail substitution.
//
// Expected diagnostic:
//   "no matching function for call to 'mint_recording_session'"

#include <crucible/bridges/RecordingSessionHandle.h>

namespace proto = ::crucible::safety::proto;

struct FakeHandle { int internal = 0; };

int main() {
    proto::SessionEventLog log{};
    FakeHandle             fake{};
    proto::RoleTagId       self{1};
    proto::RoleTagId       peer{2};

    auto bad = proto::mint_recording_session(
        std::move(fake), log, self, peer);
    (void)bad;
    return 0;
}
