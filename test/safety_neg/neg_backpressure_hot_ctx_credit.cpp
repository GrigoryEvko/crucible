#include <crucible/cntp/BackpressureRuntime.h>

// GAPS-137 fixture #3: credit mutation is background/test runtime work.
// Hot foreground replay contexts cannot block or mutate backpressure state.

int main() {
    namespace cntp = crucible::cntp;
    namespace effects = crucible::effects;
    namespace cntp = crucible::cntp;

    auto controller =
        cntp::mint_credit_flow_control<1>(effects::ColdInitCtx{});
    auto fd = cntp::admit_socket_fd(9).value();
    auto credit = cntp::admit_backpressure_credit(64).value();
    auto started = controller.start_flow(effects::HotFgCtx{}, fd, credit);
    (void)started;
    return 0;
}
