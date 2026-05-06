// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-059 fixture #3 — ctx-bound permissioned session mint rejects an
// initial PermSet that cannot be surrendered by the local protocol.  End
// with a live Permission<WorkItem> would otherwise defer the bug until
// close(); the ctx-bound mint catches it at construction.

#include <crucible/permissions/Permission.h>
#include <crucible/sessions/SessionMint.h>

#include <utility>

namespace eff   = crucible::effects;
namespace proto = crucible::safety::proto;
using ::crucible::safety::mint_permission_root;

namespace {
struct WorkItem {};
struct FakeChannel {};
}

int main() {
    eff::HotFgCtx fg;
    auto perm = mint_permission_root<WorkItem>();
    [[maybe_unused]] auto h = proto::mint_permissioned_session<proto::End>(
        fg, FakeChannel{}, std::move(perm));
    return 0;
}
