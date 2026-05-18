// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-016: HS14 floor for mint_crash_watched_endpoint<PeerTag>(Endpoint&&, flag).
//
// The first parameter is Endpoint<Substr, Dir, Ctx>&&.  Passing a bare
// int fails template argument deduction.  This fixture also supplies
// the required explicit PeerTag so the diagnostic isolates the Endpoint
// mismatch (the missing-PeerTag case is covered by the sibling fixture).
//
// Expected diagnostic:
//   "no matching function for call to 'mint_crash_watched_endpoint<PeerA>'"

#include <crucible/bridges/EndpointMint.h>
#include <crucible/handles/OneShotFlag.h>

namespace bridges = ::crucible::bridges;
namespace safety  = ::crucible::safety;

struct PeerA {};

int main() {
    safety::OneShotFlag flag;
    int                 not_an_endpoint = 42;

    auto bad = bridges::mint_crash_watched_endpoint<PeerA>(
        std::move(not_an_endpoint), flag);
    (void)bad;
    return 0;
}
