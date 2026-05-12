#include <crucible/rt/IncastControl.h>

// GAPS-124 fixture #2: socket tuning requires a DeclaredIncastConfig
// tagged with source::IncastConfig, not a raw config struct.

int main() {
    crucible::effects::ColdInitCtx init{};
    auto controller = crucible::rt::mint_incast_controller<1>(init);
    auto fd = crucible::cntp::admit_socket_fd(3);
    auto result = controller.configure_socket(
        init, *fd, crucible::cntp::IncastConfig{});
    (void)result;
    return 0;
}
