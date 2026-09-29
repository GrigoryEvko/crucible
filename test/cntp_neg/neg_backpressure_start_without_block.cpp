// Starting a flow waits on the start gate, and the wait is a block.  A
// background context that owns no Block may grant and consume credit, but
// it cannot start a flow.

#include <crucible/cntp/BackpressureRuntime.h>
#include <fixy/Ctx.h>

int main() {
    namespace cntp = crucible::cntp;
    ::fixy::ColdInitCtx init{::foundation::effects::testing::init()};
    ::fixy::BgDrainCtx drain{::foundation::effects::testing::bg()};
    auto controller = cntp::mint_credit_flow_control<1>(init);
    auto fd = cntp::admit_socket_fd(9).value();
    auto credit = cntp::admit_backpressure_credit(64).value();
    auto started = controller.start_flow(drain, fd, credit);
    return started.has_value() ? 0 : 1;
}
