// Credit bookkeeping is background or test work.  The hot foreground
// context owns neither capability, so it cannot start a flow.

#include <crucible/cntp/BackpressureRuntime.h>
#include <fixy/Ctx.h>

int main() {
    namespace cntp = crucible::cntp;
    ::fixy::ColdInitCtx init{::foundation::effects::testing::init()};
    auto controller = cntp::mint_credit_flow_control<1>(init);
    auto fd = cntp::admit_socket_fd(9).value();
    auto credit = cntp::admit_backpressure_credit(64).value();
    auto started = controller.start_flow(::foundation::effects::testing::foreground(), fd, credit);
    return started.has_value() ? 0 : 1;
}
