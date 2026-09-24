// The context-bound with_session lends a channel handle by move.  This
// call passes the handle as an lvalue, which would copy it and give the
// channel two holders while the session runs.  The handle is move-only,
// so the copy is refused.

#include <fixy/session/Entry.h>

#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

#include <utility>

namespace copies_resource_fixture {
namespace s = ::fixy::session;
struct Channel {
    Channel() = default;
    Channel(Channel&&) noexcept = default;
    Channel& operator=(Channel&&) noexcept = default;
    Channel(const Channel&) = delete("a channel handle has one holder");
    Channel& operator=(const Channel&) = delete("a channel handle has one holder");
    ~Channel() = default;
};
using Stream = s::Loop<s::Select<s::Send<int, s::Continue>, s::End>>;
}  // namespace copies_resource_fixture

int main() {
    using BgCtx = ::foundation::effects::detail::ctx_witnesses::BgWitness;
    const BgCtx ctx{::foundation::effects::testing::bg()};
    copies_resource_fixture::Channel channel{};
    auto back = ::fixy::session::with_session<copies_resource_fixture::Stream>(
        ctx, channel, [](auto head) noexcept { return std::move(head).template select_local<1>(); });
    (void)back;
    return 0;
}
