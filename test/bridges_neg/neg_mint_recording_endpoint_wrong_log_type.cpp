// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-016: HS14 floor for mint_recording_endpoint(Endpoint&&, ...).
//
// Distinct mismatch class from neg_mint_recording_endpoint_non_endpoint.cpp:
// instead of a primitive int, pass a bespoke struct that mimics the
// shape of an Endpoint-like object.  The factory's first parameter
// requires an Endpoint<Substr, Dir, Ctx> specialisation; a user-defined
// struct will never match that template, so deduction fails.
//
// Expected diagnostic:
//   "no matching function for call to 'mint_recording_endpoint'"

#include <crucible/bridges/EndpointMint.h>

namespace bridges = ::crucible::bridges;
namespace proto   = ::crucible::safety::proto;

struct FakeEndpoint {
    int internal = 0;
};

int main() {
    proto::SessionEventLog log{};
    FakeEndpoint           fake{};
    proto::RoleTagId       self{1};
    proto::RoleTagId       peer{2};

    auto bad = bridges::mint_recording_endpoint(
        std::move(fake), log, self, peer);
    (void)bad;
    return 0;
}
