// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A caller must not fabricate a PermissionedSessionHandle whose
// phantom PermSet claims authority that no consumed Permission token
// established.

#include <crucible/sessions/PermissionedSession.h>

namespace proto = ::crucible::safety::proto;

namespace {
struct WorkItem {};
struct FakeChannel {};
}

int main() {
    [[maybe_unused]] proto::PermissionedSessionHandle<
        proto::End,
        proto::PermSet<WorkItem>,
        FakeChannel> h{FakeChannel{}};
    return 0;
}
