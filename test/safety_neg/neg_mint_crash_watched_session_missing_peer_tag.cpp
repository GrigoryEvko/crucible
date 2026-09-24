// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CLAUDE.md §XXI Universal Mint Pattern + HS14: every mint factory
// ships at least 2 negative-compile fixtures demonstrating the
// soundness gate fires.
//
// mint_crash_watched_session<PeerTag>(handle, flag) requires PeerTag
// to be specified explicitly because the SAME bare SessionHandle can
// be watched against DIFFERENT peers (multi-peer sessions chain
// nested CrashWatchedHandles, one per peer).  PeerTag is therefore
// in a non-deduced context — calling the mint without it cannot
// resolve which peer the wrapper records.
//
// Violation: invoke the mint without an explicit PeerTag template
// argument.  The compiler cannot deduce PeerTag from the handle's
// type (it doesn't appear in any deducible parameter), so overload
// resolution fails.
//
// Expected diagnostic:
//   "no matching function for call to 'mint_crash_watched_session(...)'"
//   or "couldn't infer template argument 'PeerTag'".

#include <crucible/bridges/CrashTransport.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionMint.h>

namespace proto = crucible::safety::proto;
using crucible::safety::OneShotFlag;

struct DummyChannel {};

int main() {
    using P = proto::Send<int, proto::End>;

    OneShotFlag flag;
    DummyChannel ch{};

    auto bare = proto::mint_session_handle<P>(std::move(ch));

    // No <PeerTag> — PeerTag is non-deducible, the call fails to
    // resolve.  This proves the mint REQUIRES the explicit peer
    // identity at every site (no silent default, no positional
    // confusion).
    auto bad = proto::mint_crash_watched_session(std::move(bare), flag);
    (void)bad;
    return 0;
}
