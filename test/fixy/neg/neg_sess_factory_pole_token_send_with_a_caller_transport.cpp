// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The token send of the handle factory is the step that the token send of a
// handle calls.  A unit plants a pole table, in a probe region that the
// plugin rules admit, whose pole receives a token and then sends it on.  The
// pole table moves no token, so the factory refuses a write of the caller on
// a pole, as the send of the handle does.
//
// Expected diagnostic: [Pole_Owns_Its_Transport] at the send of the handle
// factory.

#include <fixy/Ctx.h>
#include <fixy/session/Handle.h>

#include <foundation/Quarantine.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <concepts>
#include <utility>

namespace factory_pole_token_send_fixture {
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
}  // namespace factory_pole_token_send_fixture

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture plants a pole table whose pole receives a token and sends it on")
namespace fixy::session::pole_tables::factory_token_send_probe {
template <class Pole>
    requires std::same_as<Pole, factory_pole_token_send_fixture::TokenChannel::ProducerHandle>
struct token_protocols {
    using Token = factory_pole_token_send_fixture::Token;
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
}  // namespace fixy::session::pole_tables::factory_token_send_probe
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() {
    using namespace factory_pole_token_send_fixture;
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto head = s::SessionMintDoor::open_pole<s::DefaultAbandonmentPolicy>(ctx, TokenChannel::ProducerHandle{});
    auto at_send = std::move(head).recv(ctx);
    auto next =
        s::HandleFactory::send(std::move(at_send), [](TokenChannel::ProducerHandle&, Token&) noexcept { return true; });
    std::move(next).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
