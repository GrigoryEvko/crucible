#include <crucible/cntp/AfXdp.h>

int main() {
    crucible::effects::ColdInitCtx init{};
    crucible::cntp::AfXdpConfig raw{};
    auto socket =
        crucible::cntp::mint_af_xdp_socket<131'072, 2'048, 64, 64, 64, 64>(
            init, raw);
    return static_cast<int>(socket.tx_pending());
}
