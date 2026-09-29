// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The permission set of the handle holds a tag, and the sender delegates
// the handle without the hold of the token of that tag.  The recipient's
// handle would then claim the region with no token, and the sender would
// keep the token of a region that it handed over.  The form of the mint
// that takes no hold admits only a handle with the empty set.
//
// Expected diagnostic: no form of the mint accepts the handle alone.
#include <fixy/Ctx.h>
#include <fixy/session/Delegate.h>
#include <foundation/effects/Row.h>

#include <utility>

namespace neg_sess_delegation_without_hold_types {
namespace s = ::fixy::session;
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
struct Wire {
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};
using Inner = s::Send<s::Transferable<int, Region>, s::End>;
}  // namespace neg_sess_delegation_without_hold_types

int main() {
    namespace s = ::fixy::session;
    using namespace neg_sess_delegation_without_hold_types;
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto [handle, hold] =
        s::mint_permissioned_session<Inner>(ctx, Wire{}, ::foundation::permissions::mint_permission_root<Region>());
    auto parcel = s::mint_delegated_session(std::move(handle));
    static_cast<void>(parcel);
    static_cast<void>(hold);
    return 0;
}
