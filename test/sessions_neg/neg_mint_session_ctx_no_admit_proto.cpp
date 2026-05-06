// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-059 fixture #2 — ctx-bound permissioned session mint must reject
// a protocol whose payload effect row is not admitted by the Ctx.  The
// Transferable wrapper is intentionally present: payload_row must see
// through permission-flow wrappers to the carried Computation row.

#include <crucible/effects/Computation.h>
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
    using BgValue = eff::Computation<eff::Row<eff::Effect::Bg>, int>;
    using Proto   = proto::Send<proto::Transferable<BgValue, WorkItem>,
                                proto::End>;

    eff::HotFgCtx fg;
    auto perm = mint_permission_root<WorkItem>();
    [[maybe_unused]] auto h = proto::mint_permissioned_session<Proto>(
        fg, FakeChannel{}, std::move(perm));
    return 0;
}
