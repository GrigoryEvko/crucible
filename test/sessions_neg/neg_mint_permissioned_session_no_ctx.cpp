// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The public permissioned-session mint is ctx-bound.  A caller must
// supply an ExecCtx so row/vendor/epoch admission runs before a handle
// exists.

#include <crucible/sessions/SessionMint.h>

namespace proto = ::crucible::safety::proto;

namespace {
struct FakeChannel {};
}

int main() {
    [[maybe_unused]] auto h =
        proto::mint_permissioned_session<proto::End>(FakeChannel{});
    return 0;
}
