// A token that crosses a session keeps the brand of its root: the protocol
// names the token type, and the receiver gets the token with its brand.  So
// the receiver holds no singleton root, and release_singleton_root, which
// takes only a token on the erased identity, refuses the token.  The tag has
// a stable identity, so the brand is the one reason of the refusal.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Mint.h>
#include <foundation/Linear.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace neg_sess_received_token_is_no_singleton_root_tags {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace neg_sess_received_token_is_no_singleton_root_tags

namespace {

namespace s = ::fixy::session;
namespace fp = ::foundation::permissions;
using neg_sess_received_token_is_no_singleton_root_tags::Region;

template <class Token>
struct Mailbox {
    ::fixy::FixedList<Token, 1> held{};
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};

}  // namespace

int main() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto root = fp::mint_permission_root<Region>();
    using Token = decltype(root);
    auto sender = s::mint_permissioned_session<s::Send<Token, s::End>>(ctx, Mailbox<Token>{}, std::move(root));
    auto sender_done = std::move(sender).send([](Mailbox<Token>& box, Token& token) noexcept {
        static_cast<void>(box.held.append(std::move(token)).expect("empty"));
        return true;
    });
    auto receiver = s::mint_session<s::Recv<Token, s::End>>(ctx, std::move(sender_done).close());
    auto receiver_done = std::move(receiver).recv([](Mailbox<Token>& box) noexcept { return box.held.take_last(); });
    auto [wire, token] = std::move(receiver_done).close();
    fp::release_singleton_root(std::move(token));
    std::move(wire.held).expect_empty("the receive takes the token out of the mailbox");
    return 0;
}
