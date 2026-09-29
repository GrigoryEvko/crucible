#include <crucible/cntp/IncastControlRuntime.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Effect.h>

// Socket tuning requires a DeclaredIncastConfig tagged with
// source::IncastConfig, not a raw config struct.  The load context passes
// the context gate, so the raw config is the only reason for the refusal.

int main() {
    ::fixy::InitLoadCtx load{::foundation::effects::testing::init()};
    auto controller = crucible::cntp::mint_incast_controller<1>(load);
    auto fd = crucible::cntp::admit_socket_fd(3);
    auto result = controller.configure_socket(load, *fd, crucible::cntp::IncastConfig{});
    (void)result;
    return 0;
}
