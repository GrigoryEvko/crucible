// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The sender delegates a handle whose set holds one tag with the hold of
// the token of another tag.  The hold backs no tag of the handle, so the
// recipient's handle would claim a region with no token.  The mint admits
// only the hold of the exact set of the handle.
//
// Expected diagnostic: no form of the mint accepts the hold.
#include <fixy/Ctx.h>
#include <fixy/session/Delegate.h>
#include <foundation/effects/Row.h>

#include <utility>

namespace neg_sess_delegation_foreign_hold_types {
namespace s = ::fixy::session;
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
struct Other {
    using permission_row = ::foundation::effects::Row<>;
};
struct Wire {
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};
using Inner = s::Send<s::Transferable<int, Region>, s::End>;
}  // namespace neg_sess_delegation_foreign_hold_types

int main() {
    namespace s = ::fixy::session;
    namespace fp = ::foundation::permissions;
    using namespace neg_sess_delegation_foreign_hold_types;
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto [handle, hold] = s::mint_permissioned_session<Inner>(ctx, Wire{}, fp::mint_permission_root<Region>());
    auto parcel =
        s::mint_delegated_session(std::move(handle), s::mint_permission_hold(fp::mint_permission_root<Other>()));
    static_cast<void>(parcel);
    static_cast<void>(hold);
    return 0;
}
