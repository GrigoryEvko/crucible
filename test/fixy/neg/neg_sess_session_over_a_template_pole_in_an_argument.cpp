// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A unit mints a plain session over a class that holds a Ref to a pole, and
// the pole is a specialization of a class template.  The pole names its
// channel with channel_type, so a session of one end over the class could
// run a protocol that is no dual of the other end.  The walk of the Resource
// reads a specialization in a template argument with the instantiating read,
// so it sees the pole, and the gate refuses the class.
//
// Expected diagnostic: no mint_session admits the class, because the
// Resource reaches a pole of a channel.

#include <fixy/Core.h>
#include <fixy/session/Entry.h>

#include <foundation/effects/Ctx.h>

#include <utility>

namespace template_pole_fixture {
struct Wire {};
template <class Payload>
struct WirePole {
    using channel_type = Wire;
    Payload last{};
};
template <class Held>
struct NamesAPole {
    Held pole;
    [[no_unique_address]] ::fixy::session::MoveOnlyResource move_only{};
};
}  // namespace template_pole_fixture

int main() {
    using namespace template_pole_fixture;
    namespace s = ::fixy::session;
    namespace eff = ::foundation::effects;
    const eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>> ctx = eff::testing::foreground();
    WirePole<int> pole{};
    auto held = ::fixy::to(pole);
    auto head = s::mint_session<s::Send<int, s::End>>(ctx, NamesAPole<decltype(held)>{.pole = held});
    std::move(head).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
