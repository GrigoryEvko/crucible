// Negative compile test.  This file must fail to compile.
//
// A unit plants a pole table, in a probe region that the plugin rules admit,
// whose pole receives a token and then sends it on.  The session receives
// the token through the read of the table, and then it sends the token with
// a write of its own.  The pole table moves the values of its channel and no
// token, so the token send over a pole has no write in the table, and the
// step refuses a write of the caller.
//
// Expected diagnostic: [Pole_Owns_Its_Transport] at the send of the handle.

#include <fixy/Ctx.h>
#include <fixy/session/Handle.h>

#include <foundation/Quarantine.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <concepts>
#include <utility>

namespace pole_token_send_fixture {
namespace s = ::fixy::session;
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
using Token = decltype(::foundation::permissions::mint_permission_root<Region>());
struct TokenChannel {
    struct ProducerHandle {
        using channel_type = TokenChannel;
        [[no_unique_address]] s::MoveOnlyResource move_only{};
    };
    struct ConsumerHandle {
        using channel_type = TokenChannel;
        [[no_unique_address]] s::MoveOnlyResource move_only{};
    };
};
}  // namespace pole_token_send_fixture

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture plants a pole table whose pole receives a token and sends it on")
namespace fixy::session::pole_tables::token_send_probe {
template <class Pole>
    requires std::same_as<Pole, pole_token_send_fixture::TokenChannel::ProducerHandle>
struct token_protocols {
    using Token = pole_token_send_fixture::Token;
    using self_protocol =
        ::fixy::session::Loop<::fixy::session::Recv<Token, ::fixy::session::Send<Token, ::fixy::session::Continue>>>;
    using peer_protocol = ::fixy::session::dual_of_t<self_protocol>;
    template <::foundation::effects::IsExecCtx Ctx>
    [[nodiscard]] static constexpr ::foundation::core::Option<Token> read(Ctx const& /*ctx*/, Pole& /*pole*/) noexcept {
        return ::foundation::core::Option<Token>{};
    }
};
inline constexpr ::fixy::session::pole_table token_table{.table = ^^token_protocols};
inline constexpr ::fixy::session::transition::seal table_seal{.kind = ^^::fixy::session::pole_table, .count = 1};
}  // namespace fixy::session::pole_tables::token_send_probe
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() {
    using namespace pole_token_send_fixture;
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto head = s::SessionMintDoor::open_pole<s::DefaultAbandonmentPolicy>(ctx, TokenChannel::ProducerHandle{});
    auto at_send = std::move(head).recv(ctx);
    auto next = std::move(at_send).send([](TokenChannel::ProducerHandle&, Token&) noexcept { return true; });
    std::move(next).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
