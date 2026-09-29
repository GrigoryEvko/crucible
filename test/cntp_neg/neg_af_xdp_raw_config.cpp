// A socket mint takes a config that came through the AF_XDP config door, so
// a bare AfXdpConfig does not convert to the tagged parameter.

#include <crucible/cntp/AfXdp.h>
#include <fixy/Ctx.h>

int main() {
    ::fixy::ColdInitCtx init{::foundation::effects::testing::init()};
    crucible::cntp::AfXdpConfig raw{};
    auto socket = crucible::cntp::mint_af_xdp_socket<131072, 2048, 64, 64, 64, 64>(init, raw);
    return static_cast<int>(socket.tx_pending());
}
