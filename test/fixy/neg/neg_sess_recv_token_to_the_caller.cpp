// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A session receives a bare Permission, and its set then holds the region of
// the token.  The receive puts the token into the slots of the handle, so the
// set and the token are one ledger, and the caller gets the handle alone.
// This file takes the token out of the result as a value beside the handle.
// Before the slots, the receive gave the token to the caller while the set
// of the handle claimed it too, and this file compiled.
//
// Expected diagnostic: the result of the receive is a handle, which a
// structured binding cannot take apart.

#include <fixy/Ctx.h>
#include <fixy/session/Entry.h>
#include <foundation/Linear.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace neg_sess_recv_token_to_the_caller_types {
namespace s = ::fixy::session;
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
// The token after the erase of its brand: the identity on which the slots
// keep each token of a set.
using Token = decltype(::foundation::permissions::permission_erase_brand(
    ::foundation::permissions::mint_permission_root<Region>()));
struct Wire {
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};
}  // namespace neg_sess_recv_token_to_the_caller_types

int main() {
    namespace s = ::fixy::session;
    namespace fp = ::foundation::permissions;
    using namespace neg_sess_recv_token_to_the_caller_types;
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto receiver = s::mint_session<s::Recv<Token, s::End>>(ctx, Wire{});
    auto [token, done] = std::move(receiver).recv(
        [](Wire&) noexcept { return ::fixy::some(fp::permission_erase_brand(fp::mint_permission_root<Region>())); });
    ::foundation::linear::drop(std::move(token));
    static_cast<void>(std::move(done).close());
    return 0;
}
