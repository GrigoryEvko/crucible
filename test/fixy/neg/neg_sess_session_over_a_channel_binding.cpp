// Negative compile test.  This file must fail to compile.
//
// A unit mints a plain session over a class that names no channel_type and
// holds a reference to a ChannelBinding of a channel.  The binding binds a
// handle to its channel, so the class reaches the channel, and a session of
// one end over it could run a protocol that is no dual of the other end.  The
// walk of the Resource tells a binding by its template, so it sees the
// binding behind the reference with no instantiation, and the gate refuses
// the class.
//
// Expected diagnostic: no mint_session admits the class, because the
// Resource reaches a pole of a channel.

#include <fixy/Core.h>
#include <fixy/session/Entry.h>

#include <foundation/ChannelBinding.h>
#include <foundation/effects/Ctx.h>

#include <utility>

namespace binding_fixture {
// A class that binds a handle to itself, as a channel does.
struct Log {
    [[nodiscard]] ::foundation::ChannelBinding<Log> bind() noexcept {
        return ::foundation::ChannelBinding<Log>{*this, ::foundation::BindingKey<Log>{}};
    }
};
template <class Binding>
struct BindsALog {
    Binding binding;
    [[no_unique_address]] ::fixy::session::MoveOnlyResource move_only{};
};
}  // namespace binding_fixture

int main() {
    using namespace binding_fixture;
    namespace s = ::fixy::session;
    namespace eff = ::foundation::effects;
    const eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>> ctx = eff::testing::foreground();
    Log log{};
    ::foundation::ChannelBinding<Log> binding = log.bind();
    auto held = ::fixy::to(binding);
    auto head = s::mint_session<s::Send<int, s::End>>(ctx, BindsALog<decltype(held)>{.binding = held});
    std::move(head).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
