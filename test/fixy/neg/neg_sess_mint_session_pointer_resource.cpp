// A raw pointer to a channel handle is copyable and reaches out, so it is
// not a SessionResource: a copy of it is a second holder of the channel,
// and the handle it points at can move away or die before the session
// ends.  The gate refuses it.  The context-bound loan is with_session,
// which takes the channel handle by move.

#include <fixy/session/Entry.h>

#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

#include <utility>

namespace pointer_resource_fixture {
namespace s = ::fixy::session;
struct Channel {
    [[no_unique_address]] s::MoveOnlyResource move_only{};
};
using Proto = s::Send<int, s::End>;
}  // namespace pointer_resource_fixture

int main() {
    using BgCtx = ::foundation::effects::detail::ctx_witnesses::BgWitness;
    const BgCtx ctx{::foundation::effects::testing::bg()};
    pointer_resource_fixture::Channel channel{};
    auto head = ::fixy::session::mint_session<pointer_resource_fixture::Proto>(ctx, &channel);
    std::move(head).detach(::fixy::session::detach_reason::TestInstrumentation{});
    return 0;
}
