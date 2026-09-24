// The context-bound with_session gives the Resource back only through the
// End handle of its own session.  This body returns its handle at the
// head of the loop, before the session ends, so no End handle comes back
// and the body gate refuses the call.  An open session cannot leave the
// body.

#include <fixy/session/Entry.h>

#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

#include <utility>

namespace returns_open_fixture {
namespace s = ::fixy::session;
struct Channel {
    [[no_unique_address]] s::MoveOnlyResource move_only{};
};
using Stream = s::Loop<s::Select<s::Send<int, s::Continue>, s::End>>;
}  // namespace returns_open_fixture

int main() {
    using BgCtx = ::foundation::effects::detail::ctx_witnesses::BgWitness;
    const BgCtx ctx{::foundation::effects::testing::bg()};
    auto back = ::fixy::session::with_session<returns_open_fixture::Stream>(
        ctx, returns_open_fixture::Channel{}, [](auto head) noexcept { return head; });
    (void)back;
    return 0;
}
