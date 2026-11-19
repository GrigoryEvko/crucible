// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A permissioned session sends a bare Permission that did not come from the
// slots of the handle.  The session opens with the token of Region, and a
// second root token of Region comes from mint_permission_root, of another
// brand, which the send then takes.  The step of a bare Permission takes the
// token from the slots of the handle and no value from the caller, so a
// token that the caller gives is refused.  Before the slots, the send read
// only the permission set, and this file compiled and sent the foreign
// token.
//
// Expected diagnostic: the deleted send of a token, [Token_From_The_Slots].

#include <fixy/Ctx.h>
#include <fixy/session/Mint.h>
#include <foundation/Linear.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace neg_sess_send_bare_token_from_outside_the_hold_types {
namespace s = ::fixy::session;
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
// The token after the erase of its brand: the identity on which the slots
// keep each token of a set.
using Token = decltype(::foundation::permissions::permission_erase_brand(
    ::foundation::permissions::mint_permission_root<Region>()));
struct OneSlot {
    int sends = 0;
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};
}  // namespace neg_sess_send_bare_token_from_outside_the_hold_types

int main() {
    namespace s = ::fixy::session;
    namespace fp = ::foundation::permissions;
    using namespace neg_sess_send_bare_token_from_outside_the_hold_types;
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto sender =
        s::mint_permissioned_session<s::Send<Token, s::End>>(ctx, OneSlot{}, fp::mint_permission_root<Region>());
    Token foreign = fp::permission_erase_brand(fp::mint_permission_root<Region>());
    auto done = std::move(sender).send(std::move(foreign), [](OneSlot& slot, Token& token) noexcept {
        ::foundation::linear::drop(std::move(token));
        ++slot.sends;
        return true;
    });
    static_cast<void>(std::move(done).close());
    return 0;
}
