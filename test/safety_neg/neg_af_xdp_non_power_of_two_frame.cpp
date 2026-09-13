#include <crucible/cntp/AfXdp.h>

int main() {
    crucible::effects::ColdInitCtx init{};
    auto cfg = crucible::cntp::DeclaredAfXdpConfig{crucible::cntp::AfXdpConfig{}};
    auto socket = crucible::cntp::mint_af_xdp_socket<131072, 1500, 64, 64, 64, 64>(init, cfg);
    return static_cast<int>(socket.tx_pending());
}
