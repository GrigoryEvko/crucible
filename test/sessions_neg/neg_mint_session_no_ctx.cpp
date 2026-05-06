// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-059 fixture #1 — mint_session is a ctx-bound mint.  Calling it
// without Ctx must be rejected at overload resolution; callers that do
// not want row admission use the lower-level mint_session_handle token
// mint explicitly.

#include <crucible/sessions/SessionMint.h>

using namespace crucible::safety::proto;

namespace {
struct FakeChannel {};
}

int main() {
    [[maybe_unused]] auto h = mint_session<End>(FakeChannel{});
    return 0;
}
