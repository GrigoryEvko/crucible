// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A unit mints a plain session over a class that refers to a class with no
// definition in the unit.  The walk of the Resource cannot read that class.
// A unit that defines the class could find a pole in it, and this unit could
// not, so the two units would hold different answers for one Resource.  The
// walk stops the build at the class that it cannot read.
//
// Expected diagnostic: a_resource_reaches_a_class_that_the_pole_walk_cannot_read,
// which has no constant definition, and no mint_session admits the class.

#include <fixy/session/Entry.h>

#include <foundation/effects/Ctx.h>

#include <utility>

namespace unreadable_fixture {
struct Opaque;
Opaque& opaque_of() noexcept;
struct RefersToOpaque {
    Opaque& opaque;
    [[no_unique_address]] ::fixy::session::MoveOnlyResource move_only{};
};
}  // namespace unreadable_fixture

int main() {
    using namespace unreadable_fixture;
    namespace s = ::fixy::session;
    namespace eff = ::foundation::effects;
    const eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>> ctx = eff::testing::foreground();
    auto head = s::mint_session<s::Send<int, s::End>>(ctx, RefersToOpaque{.opaque = opaque_of()});
    std::move(head).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
