// The context-bound with_session lends the Resource for the body only.
// This body moves its handle into a field of an object with static
// storage, so the handle would outlive the call, and it returns nothing.
// With no End handle to close, the body gate refuses the call.

#include <fixy/session/Entry.h>

#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

#include <type_traits>
#include <utility>

namespace keeps_handle_fixture {
namespace s = ::fixy::session;
struct Channel {
    [[no_unique_address]] s::MoveOnlyResource move_only{};
};
using Stream = s::Loop<s::Select<s::Send<int, s::Continue>, s::End>>;
}  // namespace keeps_handle_fixture

int main() {
    using BgCtx = ::foundation::effects::detail::ctx_witnesses::BgWitness;
    const BgCtx ctx{::foundation::effects::testing::bg()};
    auto back = ::fixy::session::with_session<keeps_handle_fixture::Stream>(
        ctx, keeps_handle_fixture::Channel{}, [](auto head) noexcept {
            struct Holder {
                std::remove_cvref_t<decltype(head)> kept;
            };
            static Holder holder{std::move(head)};
        });
    (void)back;
    return 0;
}
