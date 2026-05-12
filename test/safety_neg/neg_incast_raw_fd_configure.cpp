#include <crucible/cntp/IncastControlRuntime.h>

// GAPS-124 fixture #1: socket tuning requires an admitted SocketFd.
// Raw int descriptors cannot cross the incast-control boundary.

int main() {
    crucible::effects::ColdInitCtx init{};
    auto controller = crucible::cntp::mint_incast_controller<1>(init);
    auto config =
        crucible::cntp::mint_incast_config(crucible::cntp::IncastConfig{});
    auto result = controller.configure_socket(init, 3, *config);
    (void)result;
    return 0;
}
