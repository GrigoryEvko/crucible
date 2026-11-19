// The view of a machine bridge of fixy/session/MachineBridge.h.  A view
// runs the protocol of the bridge over a lease of its machine inside a
// body with no state, and the state that the view drove stays in the
// bridge after the view.  The negative fixtures
// neg_sess_machine_second_view_during_view and
// neg_sess_machine_extract_during_view hold the refusals.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/session/MachineBridge.h>

#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace {

namespace s = ::fixy::session;

struct Ticks {
    int count = 0;
};

using Twice = s::Send<int, s::Send<int, s::End>>;

constexpr auto add_ticks = [](auto& lease, int& value) noexcept {
    lease.state_mut().count += value;
    return true;
};

using Bridge = s::SessionFromMachine<Ticks, Twice>;

// A body with no state passes the gate, and a body that captures does not.
constexpr auto plain_body = [](auto head) noexcept { return std::move(head).send(2, add_ticks).send(3, add_ticks); };
static_assert(s::CtxFitsMachineView<::fixy::TestRunnerCtx, Twice, Bridge::lease_type, decltype(plain_body)>);
constexpr int kExtra = 1;
constexpr auto capturing_body = [extra = kExtra](auto head) noexcept {
    int first = 2 + extra;
    return std::move(head).send(first, add_ticks).send(3, add_ticks);
};
static_assert(!s::CtxFitsMachineView<::fixy::TestRunnerCtx, Twice, Bridge::lease_type, decltype(capturing_body)>,
              "a body that captures holds state of its caller");

// Two views drive the machine in turn, and the state stays in the bridge
// until the extract.
void view_drives_the_machine_of_the_bridge() {
    auto bridge = s::mint_session_from_machine<Twice, Ticks>();
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    bridge.with_view(ctx, plain_body);
    CRUCIBLE_TEST_ASSERT(kTest, bridge.state().count == 5);
    bridge.with_view(ctx, plain_body);
    const Ticks taken = std::move(bridge).extract();
    CRUCIBLE_TEST_ASSERT(kTest, taken.count == 10);
}

}  // namespace

int main() {
    view_drives_the_machine_of_the_bridge();
    ::fixy::check::pass(kTest, "test_session_machine_view: OK\n");
    return 0;
}
