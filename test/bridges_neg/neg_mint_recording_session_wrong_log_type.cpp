// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-016: HS14 floor for mint_recording_session(SessionHandle, ...).
//
// Distinct mismatch class from neg_mint_recording_session_non_handle.cpp:
// the inbound SessionHandle is well-formed, but the second parameter
// type is wrong.  The factory's second parameter is SessionEventLog&;
// passing an int fails conversion AFTER the first parameter binds, so
// the diagnostic specifically names the SessionEventLog requirement
// rather than a blanket "no overload matches".
//
// Expected diagnostic:
//   "no matching function for call to 'mint_recording_session'"
//   or "cannot bind non-const lvalue reference of type 'SessionEventLog&' to ..."

#include <crucible/bridges/RecordingSessionHandle.h>

namespace proto = ::crucible::safety::proto;

struct ProbeResource { int value = 0; };

int main() {
    using P = proto::Send<int, proto::End>;
    ProbeResource res{};
    auto bare = proto::mint_session_handle<P>(std::move(res));

    int                not_a_log = 0;
    proto::RoleTagId   self{1};
    proto::RoleTagId   peer{2};

    auto bad = proto::mint_recording_session(
        std::move(bare), not_a_log, self, peer);
    (void)bad;
    return 0;
}
