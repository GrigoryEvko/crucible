// The door of the session mints states the whole gate of the mint, also
// the row of the protocol.  The payload of this protocol does IO, and the
// background context holds no IO.  So a direct call of the door refuses
// the context, as mint_session does.

#include <fixy/Ctx.h>
#include <fixy/session/Entry.h>

#include <foundation/effects/Computation.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

#include <utility>

namespace door_row_fixture {
namespace s = ::fixy::session;
namespace eff = ::foundation::effects;
struct Channel {
    [[no_unique_address]] s::MoveOnlyResource move_only{};
};
using IoWork = eff::Computation<eff::Row<eff::Effect::IO>, int>;
using SendsIo = s::Send<IoWork, s::End>;
}  // namespace door_row_fixture

int main() {
    using BgCtx = ::fixy::BgDrainCtx;
    const BgCtx ctx{::foundation::effects::testing::bg()};
    auto head =
        ::fixy::session::SessionMintDoor::open<door_row_fixture::SendsIo, ::fixy::session::DefaultAbandonmentPolicy>(
            ctx, door_row_fixture::Channel{}, {});
    std::move(head).detach(::fixy::session::detach_reason::TestInstrumentation{});
    return 0;
}
