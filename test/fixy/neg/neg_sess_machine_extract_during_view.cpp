// A view of a machine bridge is a borrow of its machine.  This body
// captures the bridge and extracts its state while the view lives, so the
// view would drive a machine that the bridge gave away.  The gate of a
// view takes only a body with no state, so the view refuses the body.

#include <fixy/Ctx.h>
#include <fixy/session/MachineBridge.h>

#include <foundation/effects/Ctx.h>

#include <utility>

namespace extract_fixture {
struct Payload {
    int ticks = 0;
};
using Once = ::fixy::session::Send<int, ::fixy::session::End>;
constexpr auto add_ticks = [](auto& lease, int& value) noexcept {
    lease.state_mut().ticks += value;
    return true;
};
}  // namespace extract_fixture

int main() {
    using namespace extract_fixture;
    auto bridge = ::fixy::session::mint_session_from_machine<Once, Payload>();
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    bridge.with_view(ctx, [&bridge](auto head) noexcept {
        [[maybe_unused]] Payload taken = std::move(bridge).extract();
        return std::move(head).send(2, add_ticks);
    });
    return 0;
}
