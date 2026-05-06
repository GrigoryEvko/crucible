// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-059 fixture #4 — passing the same Permission<Tag> token through
// a ctx-bound mint twice would duplicate CSL authority.  PermSet now
// rejects duplicate tags structurally at the mint boundary.

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
    using Proto = proto::Loop<proto::Send<int, proto::Continue>>;

    eff::HotFgCtx fg;
    auto perm = mint_permission_root<WorkItem>();
    [[maybe_unused]] auto h = proto::mint_permissioned_session<Proto>(
        fg, FakeChannel{}, std::move(perm), std::move(perm));
    return 0;
}
