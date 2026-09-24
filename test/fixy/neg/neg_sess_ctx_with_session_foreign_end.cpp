// Every handle of a context-bound with_session carries the brand of its
// body, and the call takes back only a handle with that brand.  This body
// detaches the handle it received, mints a second session, walks that one
// to End and returns it.  The End handle carries no brand, so the body
// gate refuses the call.

#include <fixy/session/Entry.h>

#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

#include <utility>

namespace foreign_end_fixture {
namespace s = ::fixy::session;
struct Channel {
    [[no_unique_address]] s::MoveOnlyResource move_only{};
};
using Stream = s::Loop<s::Select<s::Send<int, s::Continue>, s::End>>;
}  // namespace foreign_end_fixture

int main() {
    using BgCtx = ::foundation::effects::detail::ctx_witnesses::BgWitness;
    const BgCtx ctx{::foundation::effects::testing::bg()};
    auto back = ::fixy::session::with_session<foreign_end_fixture::Stream>(
        ctx, foreign_end_fixture::Channel{}, [&ctx](auto head) noexcept {
            std::move(head).detach(::fixy::session::detach_reason::TestInstrumentation{});
            auto other =
                ::fixy::session::mint_session<foreign_end_fixture::Stream>(ctx, foreign_end_fixture::Channel{});
            return std::move(other).template select_local<1>();
        });
    (void)back;
    return 0;
}
