// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-059 fixture #1 — mint_session is no longer a live construction
// surface.  Stale no-Ctx call sites must hard-fail and move to
// mint_permissioned_session(ctx, resource, perms...) or, for tests of
// bare mechanics, mint_session_handle(resource).

#include <crucible/sessions/SessionMint.h>

using namespace crucible::safety::proto;

namespace {
struct FakeChannel {};
}

int main() {
    [[maybe_unused]] auto h = mint_session<End>(FakeChannel{});
    return 0;
}
