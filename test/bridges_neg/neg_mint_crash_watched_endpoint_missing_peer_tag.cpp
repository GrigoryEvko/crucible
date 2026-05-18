// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-016: HS14 floor for mint_crash_watched_endpoint<PeerTag>(Endpoint&&, flag).
//
// PeerTag is the FIRST template parameter and is NOT deducible from the
// function arguments (it does not appear in any deducible parameter
// position — only in the survivor_registry lookup and the eventual
// CrashWatchedHandle's compile-time identity).  Calling the factory
// without an explicit `<PeerTag>` therefore cannot resolve which peer
// the wrapper records.
//
// Even with a non-Endpoint input, the diagnostic must mention either
// "could not deduce template parameter 'PeerTag'" or "no matching
// function" — both surface the missing-tag gate.
//
// Expected diagnostic:
//   "no matching function for call to 'mint_crash_watched_endpoint'"

#include <crucible/bridges/EndpointMint.h>
#include <crucible/handles/OneShotFlag.h>

namespace bridges = ::crucible::bridges;
namespace safety  = ::crucible::safety;

int main() {
    safety::OneShotFlag flag;
    int                 not_an_endpoint = 42;

    // No <PeerTag> — PeerTag is non-deducible, the call fails to
    // resolve.  Even if it WERE somehow deducible, the int input would
    // also fail.
    auto bad = bridges::mint_crash_watched_endpoint(
        std::move(not_an_endpoint), flag);
    (void)bad;
    return 0;
}
