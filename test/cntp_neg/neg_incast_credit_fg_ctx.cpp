#include <crucible/cntp/IncastControlRuntime.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Effect.h>

// Receiver-issued credit mutation is Bg-row work.  A foreground hot-path
// context cannot issue incast credits.  The foreground context is taken as
// a parameter, because no context builds from nothing.

void issue_from_foreground(::fixy::HotFgCtx const& fg) {
    ::fixy::ColdInitCtx init{::foundation::effects::testing::init()};
    auto controller = crucible::cntp::mint_incast_controller<1>(init);
    auto fd = crucible::cntp::admit_socket_fd(3);
    auto credit = crucible::cntp::admit_credit_bytes(4096);
    auto started = controller.start_credit_flow(init, *fd, *credit);
    (void)started;
    auto result = controller.issue_credit(fg, *fd, *credit, 1);
    (void)result;
}

int main() { return 0; }
