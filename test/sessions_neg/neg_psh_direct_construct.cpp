// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// PermissionedSessionHandle construction is factory-only.  Direct
// construction would bypass ctx admission and local permission-flow
// closure.

#include <crucible/sessions/PermissionedSession.h>

namespace proto = ::crucible::safety::proto;

namespace {
struct FakeChannel {};
}

int main() {
    [[maybe_unused]] proto::PermissionedSessionHandle<
        proto::End,
        proto::EmptyPermSet,
        FakeChannel> h{FakeChannel{}};
    return 0;
}
