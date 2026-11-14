// A view of a machine bridge is a borrow of its machine.  This body
// captures the bridge and opens a second view of it while the first view
// lives, so two handles would drive one machine against each other.  The
// gate of a view takes only a body with no state, so the outer view
// refuses the body.

#include <fixy/Ctx.h>
#include <fixy/session/MachineBridge.h>

#include <foundation/effects/Ctx.h>

#include <utility>

namespace second_view_fixture {
struct Payload {
    int ticks = 0;
};
using Once = ::fixy::session::Send<int, ::fixy::session::End>;
constexpr auto add_ticks = [](auto& lease, int& value) noexcept {
    lease.state_mut().ticks += value;
    return true;
};
}  // namespace second_view_fixture

int main() {
    using namespace second_view_fixture;
    auto bridge = ::fixy::session::mint_session_from_machine<Once, Payload>();
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    bridge.with_view(ctx, [&bridge, &ctx](auto head) noexcept {
        bridge.with_view(ctx, [](auto inner) noexcept { return std::move(inner).send(1, add_ticks); });
        return std::move(head).send(2, add_ticks);
    });
    return 0;
}
