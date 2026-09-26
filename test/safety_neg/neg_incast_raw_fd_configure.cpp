#include <crucible/cntp/IncastControlRuntime.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Effect.h>

// Socket tuning requires an admitted SocketFd.  A raw int descriptor
// cannot cross the incast-control boundary.  The load context passes the
// context gate, so the descriptor is the only reason for the refusal.

int main() {
    ::fixy::InitLoadCtx load{::foundation::effects::testing::init()};
    auto controller = crucible::cntp::mint_incast_controller<1>(load);
    auto config = crucible::cntp::mint_incast_config(crucible::cntp::IncastConfig{});
    auto result = controller.configure_socket(load, 3, *config);
    (void)result;
    return 0;
}
