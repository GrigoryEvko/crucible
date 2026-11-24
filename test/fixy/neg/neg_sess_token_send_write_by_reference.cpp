// The write of a token takes the token by move and gives it back while it has
// no room.  This write has the shape of a copyable payload: it only looks at
// the token and says that it took it, so the token would end in the handle
// with no consume.  The send of a token refuses the write by name.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Mint.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace neg_sess_token_send_write_by_reference_types {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace neg_sess_token_send_write_by_reference_types

namespace {

namespace s = ::fixy::session;
namespace fp = ::foundation::permissions;
using neg_sess_token_send_write_by_reference_types::Region;

struct Wire {
    int tokens = 0;
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};

}  // namespace

int main() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto root = fp::mint_permission_root<Region>();
    using Token = decltype(root);
    auto sender = s::mint_permissioned_session<s::Send<Token, s::End>>(ctx, Wire{}, std::move(root));
    auto sender_done = std::move(sender).send([](Wire& wire, Token& /*token*/) noexcept -> bool {
        wire.tokens = 1;
        return true;
    });
    static_cast<void>(std::move(sender_done).close());
    return 0;
}
