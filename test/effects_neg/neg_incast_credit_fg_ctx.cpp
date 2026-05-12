#include <crucible/rt/IncastControl.h>

// GAPS-124 fixture #3: receiver-issued credit mutation is Bg-row work.
// Foreground hot-path contexts cannot issue incast credits.

int main() {
    crucible::effects::ColdInitCtx init{};
    crucible::effects::HotFgCtx fg{};
    auto controller = crucible::rt::mint_incast_controller<1>(init);
    auto fd = crucible::cntp::admit_socket_fd(3);
    auto credit = crucible::cntp::admit_credit_bytes(4096);
    auto started = controller.start_credit_flow(init, *fd, *credit);
    (void)started;
    auto result = controller.issue_credit(fg, *fd, *credit, 1);
    (void)result;
    return 0;
}
