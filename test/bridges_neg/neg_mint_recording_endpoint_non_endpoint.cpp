// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-016: HS14 floor for mint_recording_endpoint(Endpoint&&, ...).
//
// The first parameter is Endpoint<Substr, Dir, Ctx>&&.  Passing a bare
// int fails to bind the rvalue reference and fails template argument
// deduction.
//
// Expected diagnostic:
//   "no matching function for call to 'mint_recording_endpoint'"

#include <crucible/bridges/EndpointMint.h>

namespace bridges = ::crucible::bridges;
namespace proto   = ::crucible::safety::proto;

int main() {
    proto::SessionEventLog log{};
    int                    not_an_endpoint = 42;
    proto::RoleTagId       self{1};
    proto::RoleTagId       peer{2};

    auto bad = bridges::mint_recording_endpoint(
        std::move(not_an_endpoint), log, self, peer);
    (void)bad;
    return 0;
}
