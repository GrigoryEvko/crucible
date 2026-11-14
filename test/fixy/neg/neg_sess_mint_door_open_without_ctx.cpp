// The door of the session mints is public, and a direct call of a member
// is no weaker than the mint.  So the member that opens a session takes
// the execution context too.  This call gives no context, so the member
// is not viable.

#include <fixy/session/Entry.h>

#include <foundation/effects/Computation.h>
#include <foundation/effects/Effect.h>

#include <utility>

namespace door_without_ctx_fixture {
namespace s = ::fixy::session;
namespace eff = ::foundation::effects;
struct Channel {
    [[no_unique_address]] s::MoveOnlyResource move_only{};
};
using IoWork = eff::Computation<eff::Row<eff::Effect::IO>, int>;
using SendsIo = s::Send<IoWork, s::End>;
}  // namespace door_without_ctx_fixture

int main() {
    auto head = ::fixy::session::SessionMintDoor::open<door_without_ctx_fixture::SendsIo,
                                                       ::fixy::session::DefaultAbandonmentPolicy>(
        door_without_ctx_fixture::Channel{}, {});
    std::move(head).detach(::fixy::session::detach_reason::TestInstrumentation{});
    return 0;
}
