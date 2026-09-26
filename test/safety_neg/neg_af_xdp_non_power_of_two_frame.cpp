// A socket's frame size is a template argument, and the static shape refuses
// one the kernel does not accept: 1500 bytes is not a power of two.

#include <crucible/cntp/AfXdp.h>
#include <fixy/Ctx.h>

int main() {
    ::fixy::ColdInitCtx init{::foundation::effects::testing::init()};
    auto cfg = ::fixy::mint_tagged<::fixy::tags::source::AfXdp>(crucible::cntp::AfXdpConfig{});
    auto socket = crucible::cntp::mint_af_xdp_socket<131072, 1500, 64, 64, 64, 64>(init, cfg);
    return static_cast<int>(socket.tx_pending());
}
